// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

#define MIN(a,b)  (((a) < (b)) ? (a) : (b))
#define MAX(a,b)  (((a) > (b)) ? (a) : (b))

bool writeArgBuffer(char* buf, int size, int virtAddr);

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::Initialize
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

bool AddrSpace::Initialize(OpenFile *executable, int argc, char** argv,  int* userSpaceArgv)
{

    NoffHeader noffH;
    unsigned int i, size;

    int argSize = argc * sizeof(char*); //Asumimos q es lo mismo en el comp cruzado
    for (int j = 0; j < argc; j++){
      argSize += strlen(argv[j])+1;
    }

    DEBUG('a', "Address space argument size: %d\n", argSize);

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
      SwapHeader(&noffH);

    if (noffH.noffMagic != NOFFMAGIC)
      return false;

    // how big is the address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize + argSize;
    // we need to increase the size
    // to leave room for the stack and the arguments

    size += 4; //This is to make sure we can write the args starting at an 4-aligned address

    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    if (numPages > NumPhysPages)
      return false;
    // check we're not trying
    // to run anything too big --
    // at least until we have
    // virtual memory

    if (numPages > usedVirtPages.size() - usedVirtPages.count())
      return false;

    DEBUG('a', "Initializing address space, numPages: %d\t size: 0x%x\n"
	  ,numPages,size);


// first, set up the translation 
    pageTable = new TranslationEntry[numPages];
    int j = 0;
    for (i = 0; i < numPages; i++){
      for(;usedVirtPages[j] && j < usedVirtPages.size(); j++);

      pageTable[i].virtualPage = j;	// for now, virtual page # = phys page #
      pageTable[i].physicalPage = j;
      pageTable[i].valid = TRUE;
      pageTable[i].use = FALSE;
      pageTable[i].dirty = FALSE;
      pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only

      // zero out the address space
      bzero(machine->mainMemory+PageSize*j, PageSize);

      j++;
      }
    
// zero out the entire address space, to zero the unitialized data segment 
// and the stack segment
    // bzero(machine->mainMemory, size);

    ///////// Copy in the code segment into memory /////////////////

    DEBUG('a', "Initializing code segment. VirtAddr:  0x%x\t inFileAddr: 0x%x\t size 0x%x\n", 
	  noffH.code.virtualAddr,noffH.code.inFileAddr, noffH.code.size);

    int unusedInitialPages = noffH.code.virtualAddr / PageSize;
    int initialOffset = noffH.code.virtualAddr % PageSize;
    int activePage = unusedInitialPages;
    int charsRead      = 0;   
    int memoryOffset = pageTable[activePage].physicalPage * PageSize;
    memoryOffset +=  initialOffset;
    int fileReadPoint = noffH.code.inFileAddr;
    int  charsToRead = MIN(noffH.code.size, PageSize-initialOffset); 
    

    while (noffH.code.size > charsRead){

      charsRead += executable->ReadAt(machine->mainMemory + memoryOffset,
				      charsToRead,
				      fileReadPoint);
      activePage++;
      fileReadPoint = noffH.code.inFileAddr + charsRead;
      memoryOffset = pageTable[activePage].physicalPage * PageSize;

      charsToRead = MIN(PageSize, noffH.code.size - charsRead);
    }
    
    if (charsRead != noffH.code.size){
      std::cout << "Error: Couldn't load executable file." << std::endl;
      return false;
    }

    ///////// Copy in the data segment into memory /////////////////

    DEBUG('a', "Initializing data segment. VirtAddr:  0x%x\t inFileAddr: 0x%x\t size 0x%x\n", 
    	  noffH.initData.virtualAddr,noffH.initData.inFileAddr, noffH.initData.size);

    unusedInitialPages = noffH.initData.virtualAddr / PageSize;
    initialOffset = noffH.initData.virtualAddr%PageSize;
    activePage = unusedInitialPages;
    charsRead       = 0;   
    memoryOffset = pageTable[activePage].physicalPage * PageSize;
    memoryOffset +=  initialOffset;
    fileReadPoint = noffH.initData.inFileAddr;
    charsToRead = MIN(noffH.initData.size, PageSize - initialOffset); 
    

    while (noffH.initData.size > charsRead){

      charsRead += executable->ReadAt(machine->mainMemory + memoryOffset,
				      charsToRead,
				      fileReadPoint);
      activePage++;
      fileReadPoint = noffH.initData.inFileAddr + charsRead;
      memoryOffset = pageTable[activePage].physicalPage * PageSize;

      charsToRead = MIN(PageSize, noffH.initData.size - charsRead);
    }

    if (charsRead != noffH.initData.size){
      std::cout << "Error: Couldn't load executable file." << std::endl;
      return false;
    }

    ////////////////////////////////////////////////////////////////////////
    
    for (i = 0; i < numPages; i++)
      usedVirtPages.set(pageTable[i].physicalPage,true);

    int initialArgAddress = divRoundUp(size - UserStackSize - argSize, 4)*4; //To make sure it's aligned

    if (userSpaceArgv != NULL)
      *userSpaceArgv = initialArgAddress;

    return true;
}

    //Copy args into user space
void AddrSpace::copyArguments(int argc, char** argv, int initialArgAddress)
{

    int argSize = argc * sizeof(char*); //Asumimos q es lo mismo en el comp cruzado
    for (int j = 0; j < argc; j++){
      argSize += strlen(argv[j])+1;
    }

    char* argBuffer = new char[argSize];
    
    int pointerOffset = 0;
    int dataOffset = pointerOffset + argc * sizeof(char*);

    for (int j = 0 ; j < argc ; j++){
      
      *((int*)(argBuffer + pointerOffset)) = initialArgAddress + dataOffset;
      strcpy(argBuffer + dataOffset, argv[j]);
      
      pointerOffset += sizeof(char*);
      dataOffset += strlen(argv[j]) + 1;
    }

    // for (int i = 0; i < argc; i++)
    //   printf("\n%d",((int*)argBuffer)[i]);

    // for (int i = argc*4; i < argSize; i++)
    //   printf("\n%c",argBuffer[i]);

    // exit(0);

    writeArgBuffer(argBuffer, argSize, initialArgAddress);

    delete argBuffer;
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace() {

  if (pageTable != NULL){
    // Mark the pages as free now
    for (int i = 0; i < numPages; i++)
      usedVirtPages.set(pageTable[i].virtualPage,false);
 
    delete pageTable;
  }
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);


    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
  
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
  machine->pageTable = pageTable;
  machine->pageTableSize = numPages;
}

bool writeArgBuffer(char* buf, int size, int virtAddr){
  for (int i = 0; i < size ; i++,virtAddr++){
    if (!machine->WriteMem(virtAddr, 1, buf[i]))
      return false;
  }
  return true;
}
