// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "noff.h"
#include "swap.h"

#define UserStackSize		1024 	// increase this as necessary!

class AddrSpace {
 public:

  AddrSpace(){pageTable = NULL;}
  ~AddrSpace();			// De-allocate an address space

  bool Initialize(OpenFile *executable, 
		  int argc = 0, 
		  char** argv = NULL,  
		  int* userSpaceArgv = NULL);
  // Create an address space,
  // initializing it with the program
  // stored in the file "executable"

  bool loadPageOnDemand(int page, int frame);

  void InitRegisters();		// Initialize user-level CPU registers,
  // before jumping to user code

  void SaveState();			// Save/restore address space-specific
  void RestoreState();		// info on a context switch 

  TranslationEntry pageTableEntry(int vpage); //Get entry from page table
  void savePageTableEntry(TranslationEntry entry, int vpage){  // Save an entry to the page table
    ASSERT(vpage >= 0 && vpage < numPages);
    pageTable[vpage] = entry;
  }

  void setValidity(int vpage, bool valid){
    ASSERT(vpage >= 0 && vpage < numPages);
    pageTable[vpage].valid = valid;
  }
  
  void setDirty(int vpage, bool dirty){
    ASSERT(vpage >= 0 && vpage < numPages);
    pageTable[vpage].dirty = dirty;
  }

  //Copy initial arguments into address space
  void copyArguments(int argc, char** argv, int initialArgAddress);

  int getPageNumber(int frame){
    for (int i = 0; i < numPages; i++)
      if (pageTable[i].physicalPage == frame &&
	  pageTable[i].valid)
	return i;

    return -1;
  }

 private:
  TranslationEntry *pageTable;

  OpenFile* executable;
  NoffHeader noffH;

 public:
  
  unsigned int numPages; // Number of pages in the virtual address space
  Swap* swap;

};

#endif // ADDRSPACE_H
