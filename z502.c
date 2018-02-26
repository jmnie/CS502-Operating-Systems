/*********************************************************************

 z502.c

 This is the start of the code and declarations for the Z502 simulator.

 Revision History:
 1.0 August      1990  Initial coding
 1.1 December    1990: Lots of minor problems cleaned up.
 Portability attempted.
 1.4 December    1992  Lots of minor changes.
 1.5 August      1993  Lots of minor changes.
 1.6 June        1995  Significant debugging aids added.
 1.7 Sept.       1999  Minor compile issues.
 2.0 Jan         2000  A number of fixes and improvements.
 2.1 May         2001  Bug Fixes:
 Disks no longer make cylinders visible
 Hardware displays resource usage
 2.2 July        2002  Make appropriate for undergraduates.
 2.3 Dec         2002  Allow Mem calls from OS - conditional on POP_THE_STACK
 Clear the STAT_VECTOR at the end of SoftwareTrap
 3.0 August      2004: Modified to support memory mapped IO
 3.1 August      2004: hardware interrupt runs on separate thread
 3.11 August     2004: Support for OS level locking
 3.12 Sept.      2004: Make threads schedule correctly
 3.14 November   2004: Impliment DO_DEVICE_DEBUG in meaningful way.
 3.15 November   2004: More DO_DEVICE_DEBUG to handle disks
 3.20 May        2005: Get threads working on windows
 3.26 October    2005: Make the hardware thread safe.
 3.30 July       2006: Threading works on multiprocessor
 3.40 July       2008: Minor improvements
 3.50 August     2009: Fixes for 64 bit machines and multiprocessor
 3.51 Sept.      2011: Fixed locking issue due to MemoryCommon - Linux
 3.52 Sept.      2011: Fixed locking issue due to MemoryCommon - Linux
 Release the lock when leaving MemoryCommon
 3.60  August    2012: Used student supplied code to add support
 for Mac machines
 4.00  May       2013: Major revision to the way the simulator works.
 It now runs off of multiple threads, and those
 threads start in the test code avoiding many
 many hacks.
 4.02 December   2013: STAT_VECTOR not thread safe.  Defined a method that
 uses thread info to keep things sorted out.
 4.03 December   2013: Store Z502_MODE on context save
 4.10 June       2014: Many Many fixes
 Disk transfer now doesn't occur until interrupt time.
 4.20 January    2015: Make thread safe.  Implement multiprocessors
 4.30 January    2016: Many small bug fixes.
 ************************************************************************/

/************************************************************************

 GLOBAL VARIABLES:  These declarations are visible to both the
 Z502 simulator and the OS502 if the OS declares them
 extern.

 ************************************************************************/

#define                   HARDWARE_VERSION  "4.40"

#include                 "global.h"
#include                 "syscalls.h"
#include                 "z502.h"
#include                 "protos.h"
#include                 <stdio.h>
#include                 <stdlib.h>
#include                 <memory.h>
#ifdef NT
#include                 <windows.h>
#include                 <winbase.h>
#include                 <sys/types.h>
#endif

#ifdef LINUX
#include                 <pthread.h>
#include                 <unistd.h>
#include                 <asm/errno.h>
#include                 <sys/time.h>
#include                 <sys/resource.h>
#endif

#ifdef MAC
#include                 <pthread.h>
#include                 <unistd.h>
#include                 <errno.h>
#include                 <sys/time.h>
#include                 <sys/resource.h>
#endif

//  These are routines internal to the hardware, not visible to the OS
//  Prototypes that allow the OS to get to this hardware are in protos.h

void AddEventToInterruptQueue(INT32, INT16, INT16, EVENT **);
void AssociateContextWithProcess(Z502CONTEXT *Context);
void ChargeTimeAndCheckEvents(INT32);
int CreateAThread(void *ThreadStartAddress, INT32 *data);
void CreateLock(INT32 *, char *CallingRoutine);
void CreateCondition(UINT32 *);
void CreateSectorStruct(INT16, INT16, char **);
void DequeueItemFromEventQueue(EVENT *, INT32 *);
void DoMemoryDebug(INT16, INT16);
void DoSleep(INT32 millisecs);
Z502CONTEXT *GetCurrentContext();
int GetLock(UINT32 RequestedMutex, char *CallingRoutine);
INT16 GetMode();
UINT16 *GetPageTableAddress();
int GetProcessorID();
void GetNextEventTime(INT32 *);
void GetSectorStructure(INT16, INT16, char **, INT32 *);
void GetNextOrderedEvent(INT32 *, INT16 *, INT16 *, INT32 *);
int GetMyTid();
int GetTryLock(UINT32 RequestedMutex, char *CallingRoutine);
void GoToExit(int);
void HaltSimulation();
void HandleWindowsError();
void HardwareClock(INT32 *);
void HardwareTimer(INT32);
void HardwareReadDisk(INT16, INT16, char *);
void HardwareWriteDisk(INT16, INT16, char *);
void HardwareCheckDisk(int DiskID);
void HardwareInterrupt(void);
void HardwareFault(INT16, INT16);
void HardwareInternalPanic(INT32);
void IdleSimulation();
void MakeContext(long *ReturningContextPointer, long starting_address,
		UINT16* PageTable, BOOL user_or_kernel);
void MemoryCommon(INT32, char *, BOOL);
void PhysicalMemoryCommon(INT32, char *, BOOL);
void MemoryMappedIO(INT32, MEMORY_MAPPED_IO *, BOOL);
void PrintRingBuffer(void);
void PrintHardwareStats(void);
void PrintEventQueue();
void PrintLockDebug(int Action, char *LockCaller, int Mutex, int Return);
void PrintThreadTable(char *Explanation);
int ReleaseLock(UINT32 RequestedMutex, char* CallingRoutine);
void ResumeProcessExecution(Z502CONTEXT *Context);
void SetCurrentContext(Z502CONTEXT *Address);
void SetMode(INT16 mode);
void SetPageTableAddress(UINT16 *address);
int SignalCondition(UINT32 Condition, char* CallingRoutine);
void SoftwareTrap(SYSTEM_CALL_DATA *SystemCallData);
void SuspendProcessExecution(Z502CONTEXT *Context);
void SwitchContext(void **, BOOL);
int WaitForCondition(UINT32 Condition, UINT32 Mutex, INT32 WaitTime,
		char * Caller);
void Z502Init();

INT32 STAT_VECTOR[SV_DIMENSION ][LARGEST_STAT_VECTOR_INDEX + 1];
void *TO_VECTOR[TO_VECTOR_TYPES ];

/*****************************************************************

 These declarations are USED by the entire simulation.  However,
 they are protected from access by anyone other than the Z502.c.
 The can be accessed via methods available in z502.c.

 *****************************************************************/

// This is the definition of the physical memory supported by the hardware
char MEMORY[NUMBER_PHYSICAL_PAGES * PGSIZE ];

// The hardware keeps track of the address of the context currently being run
//Z502CONTEXT *Z502_CURRENT_CONTEXT[MAX_NUMBER_OF_PROCESSORS ];

// The hardware keeps track of the address of the page table currently in use
//  IT MAY turn out we don't need this since we can find it in the context
//UINT16 *Z502_PAGE_TBL_ADDR[MAX_NUMBER_OF_PROCESSORS ];

// The hardware keeps track of the mode - user or kernel of the processor
//INT16 Z502_MODE[MAX_NUMBER_OF_PROCESSORS ];    // Kernel or user - hardware only

// This defines the number of processors used in the simulation
INT16 Z502_CURRENT_NUMBER_OF_PROCESSORS = 1;

/*****************************************************************

 LOCAL VARIABLES:  These declarations should be visible only
 to the code in z502.c

 *****************************************************************/
INT16 Z502Initialized = FALSE;
UINT32 CurrentSimulationTime = 0;
INT16 EventRingBuffer_index = 0;

EVENT EventQueue;
INT32 NumberOfInterruptsStarted = 0;
INT32 NumberOfInterruptsCompleted = 0;
SECTOR sector_queue[MAX_NUMBER_OF_DISKS ];
DISK_STATE DiskState[MAX_NUMBER_OF_DISKS ];
TIMER_STATE timer_state;
HARDWARE_STATS HardwareStats;

RING_EVENT EventRingBuffer[EVENT_RING_BUFFER_SIZE];
INT32 InterlockRecord[MEMORY_INTERLOCK_SIZE];
INT32 EventLock = -1;                          // Change from UINT32 - 08/2012
INT32 InterruptLock = -1;
INT32 HardwareLock = -1;
INT32 ThreadTableLock = -1;
INT32 SPPrintLock = -1;
INT32 MPPrintLock = -1;

UINT32 InterruptCondition = 0;
int NextConditionToAllocate = 1;    // This was 0 and seemed to work
int InterruptTid;                   // The Task ID of the interrupt thread

// Contains info about all the threads created
THREAD_INFO ThreadTable[MAX_THREAD_TABLE_SIZE];

#ifdef   NT
HANDLE LocalEvent[100];
#endif

#if defined LINUX || defined MAC
pthread_mutex_t LocalMutex[300];
pthread_cond_t LocalCondition[100];
int NextMutexToAllocate = 0;
#endif

/*****************************************************************
 MemoryCommon

 This code simulates a memory access.  Actions include:
 o Take a page fault if any of the following occur;
 + Illegal virtual address,
 + Page table doesn't exist,
 + Address is larger than page table,
 + Page table entry exists, but page is invalid.
 o The page exists in physical memory, so get the physical address.
 Be careful since it may wrap across frame boundaries.
 o Copy data to/from caller's location.
 o Set referenced/modified bit in page table.
 o Advance time and see if an interrupt has occurred.
 *****************************************************************/

void MemoryCommon(INT32 VirtualAddress, char *data_ptr, BOOL read_or_write) {
	INT16 VirtualPageNumber;
	INT32 phys_pg;
	INT16 PhysicalAddress[4];
	INT32 page_offset;
	INT16 index;
	INT32 ptbl_bits;
	INT16 invalidity;
	BOOL page_is_valid;
	char Debug_Text[32];

	strcpy(Debug_Text, "MemoryCommon");
	GetLock(HardwareLock, "MemoryCommon#1");

	if (VirtualAddress >= Z502MEM_MAPPED_MIN) {
		MemoryMappedIO(VirtualAddress, (MEMORY_MAPPED_IO *) data_ptr,
				read_or_write);
		ReleaseLock(HardwareLock, "MemoryCommon#2");
		return;
	}
	VirtualPageNumber = (INT16) ((VirtualAddress >= 0) ? VirtualAddress / PGSIZE : -1);
	page_offset = VirtualAddress % PGSIZE;

	page_is_valid = FALSE;

	/*  Loop until the virtual page passes all the tests        */

	while (page_is_valid == FALSE ) {
		invalidity = 0;          //software trap
		if (VirtualPageNumber >= NUMBER_VIRTUAL_PAGES)
			invalidity = 1;    //cpu error
		if (VirtualPageNumber < 0)
			invalidity = 2;   //invalid memory
		if (GetPageTableAddress() == NULL)
			invalidity = 3;  //invalid physical memory
		if ((invalidity == 0)
				&& (GetPageTableAddress()[(UINT16) VirtualPageNumber]
						& PTBL_VALID_BIT) == 0)
			invalidity = 4;  //PRIVILEGED_INSTRUCTION

		DoMemoryDebug(invalidity, VirtualPageNumber);

		if (invalidity > 0) {
			if ((GetCurrentContext() != NULL)
					&& ((GetCurrentContext()->StructureID)
							!= CONTEXT_STRUCTURE_ID )) {
				printf(
						"The address of the current context is invalid in MemoryCommon\n");
				printf("Something in the OS has destroyed this location.\n");
				HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
			}
			GetCurrentContext()->FaultInProgress = TRUE;
			// The fault handler will do it's own locking - 11/13/11
			ReleaseLock(HardwareLock, "MemoryCommon#3");
			HardwareFault(INVALID_MEMORY, VirtualPageNumber);
			// Regain the lock to protect the memory check - 11/13/11
			GetLock(HardwareLock, "MemoryCommon#4");
		} else
			page_is_valid = TRUE;
	} /* END of while         */

	phys_pg = GetPageTableAddress()[VirtualPageNumber] & PTBL_PHYS_PG_NO;
	PhysicalAddress[0] = (INT16) (phys_pg * (INT32) PGSIZE + page_offset);
	PhysicalAddress[1] = PhysicalAddress[0] + 1; /* first guess */
	PhysicalAddress[2] = PhysicalAddress[0] + 2; /* first guess */
	PhysicalAddress[3] = PhysicalAddress[0] + 3; /* first guess */

	page_is_valid = FALSE;
	if (page_offset > PGSIZE - 4) /* long int wraps over page */
	{
		while (page_is_valid == FALSE ) {
			invalidity = 0;
			if (VirtualPageNumber + 1 >= NUMBER_VIRTUAL_PAGES)
				invalidity = 6;
			if ((GetPageTableAddress()[(UINT16) VirtualPageNumber + 1]
					& PTBL_VALID_BIT) == 0)
				invalidity = 8;
			DoMemoryDebug(invalidity, (short) (VirtualPageNumber + 1));
			if (invalidity > 0) {
				if (GetCurrentContext() != NULL
						&& GetCurrentContext()->StructureID
								!= CONTEXT_STRUCTURE_ID) {
					printf("The address of the current context is invalid\n");
					printf("in MemoryCommon.  The OS has destroyed the \n");
					printf("location holding the context.\n");
					HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
				}
				GetCurrentContext()->FaultInProgress = TRUE;

				HardwareFault(INVALID_MEMORY, (INT16) (VirtualPageNumber + 1));
			} else
				page_is_valid = TRUE;
		} /* End of while         */

		phys_pg =
				GetPageTableAddress()[VirtualPageNumber + 1] & PTBL_PHYS_PG_NO;
		for (index = PGSIZE - (INT16) page_offset; index <= 3; index++)
			PhysicalAddress[index] = (INT16) ((phys_pg - 1) * (INT32) PGSIZE
					+ page_offset + (INT32) index);
	} /* End of if page       */

	if (phys_pg < 0 || phys_pg > NUMBER_PHYSICAL_PAGES - 1) {
		printf("The physical address is invalid in MemoryCommon\n");
		printf("Physical page = %d, Virtual Page = %d\n", phys_pg,
				VirtualPageNumber);
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	if (GetCurrentContext() != NULL
			&& GetCurrentContext()->StructureID != CONTEXT_STRUCTURE_ID) {
		printf("The address of the context is invalid in MemoryCommon\n");
		printf("Something in the OS has destroyed this location.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	GetCurrentContext()->FaultInProgress = FALSE;

	if (read_or_write == SYSNUM_MEM_READ) {
		data_ptr[0] = MEMORY[PhysicalAddress[0]];
		data_ptr[1] = MEMORY[PhysicalAddress[1]];
		data_ptr[2] = MEMORY[PhysicalAddress[2]];
		data_ptr[3] = MEMORY[PhysicalAddress[3]];
		ptbl_bits = PTBL_REFERENCED_BIT;
	}

	if (read_or_write == SYSNUM_MEM_WRITE) {
		MEMORY[PhysicalAddress[0]] = data_ptr[0];
		MEMORY[PhysicalAddress[1]] = data_ptr[1];
		MEMORY[PhysicalAddress[2]] = data_ptr[2];
		MEMORY[PhysicalAddress[3]] = data_ptr[3];
		ptbl_bits = PTBL_REFERENCED_BIT | PTBL_MODIFIED_BIT;
	}

	GetPageTableAddress()[VirtualPageNumber] |= ptbl_bits;
	if (page_offset > PGSIZE - 4)
		GetPageTableAddress()[VirtualPageNumber + 1] |= ptbl_bits;

	ChargeTimeAndCheckEvents(COST_OF_MEMORY_ACCESS);

	ReleaseLock(HardwareLock, "MemoryCommon#5");
}                      // End of MemoryCommon

/*****************************************************************
 DoMemoryDebug

 Print out details about why a page fault occurred.

 *****************************************************************/

void DoMemoryDebug(INT16 invalidity, INT16 vpn) {
	if (DO_MEMORY_DEBUG == 0)
		return;
	printf("MEMORY DEBUG: ");
	if (invalidity == 0) {
		printf("Virtual Page Number %d was successful\n", vpn);
	}
	if (invalidity == 1) {
		printf("You asked for a virtual page, %d, greater than the\n", vpn);
		printf("\t\tmaximum number of virtual pages, %d\n",
		NUMBER_VIRTUAL_PAGES);
	}
	if (invalidity == 2) {
		printf("You asked for a virtual page, %d, less than\n", vpn);
		printf("\t\tzero.  What are you thinking?\n");
	}
	if (invalidity == 3) {
		printf("A valid page table should have been set up when \n");
		printf("\t\tthe process was created.  Somehow that didn't");
		printf("\t\thappen and the hardware can't find\n");
		printf("\t\taddress of the page table.\n");
	}
	if (invalidity == 4) {
		printf("You have not initialized the slot in the page table\n");
		printf("\t\tcorresponding to virtual page %d\n", vpn);
		printf("\t\tYou must aim this virtual page at a physical frame\n");
		printf("\t\tand mark this page table slot as valid.\n");
	}
	if (invalidity == 6) {
		printf("The address you asked for crosses onto a second page.\n");
		printf("\t\tThis second page took a fault.\n");
		printf("You asked for a virtual page, %d, greater than the\n", vpn);
		printf("\t\tmaximum number of virtual pages, %d\n",
		NUMBER_VIRTUAL_PAGES);
	}

	if (invalidity == 8) {
		printf("The address you asked for crosses onto a second page.\n");
		printf("\t\tThis second page took a fault.\n");
		printf("You have not initialized the slot in the page table\n");
		printf("\t\tcorresponding to virtual page %d\n", vpn);
		printf("\t\tYou must aim this virtual page at a physical frame\n");
		printf("\t\tand mark this page table slot as valid.\n");
	}
}                        // End of DoMemoryDebug

/*****************************************************************
 Z502MemoryRead   and   Z502_MEM_WRITE

 Set a flag and call common code

 *****************************************************************/

void Z502MemoryRead(INT32 VirtualAddress, INT32 *data_ptr) {

	MemoryCommon(VirtualAddress, (char *) data_ptr, (BOOL) SYSNUM_MEM_READ);
}                  // End  Z502MemoryRead

void Z502MemoryWrite(INT32 VirtualAddress, INT32 *data_ptr) {

	MemoryCommon(VirtualAddress, (char *) data_ptr, (BOOL) SYSNUM_MEM_WRITE);
}                  // End  Z502MemoryWrite

/*************************************************************************
 Z502MemoryReadModify

 Do atomic modify of a memory location.  If the input parameters are
 incorrect, then return SuccessfulAction = FALSE.
 If the memory location
 is already locked by someone else, and we're asked to lock
 return SuccessfulAction = FALSE.
 If the lock was obtained here, return  SuccessfulAction = TRUE.
 If the lock was held by us or someone else, and we're asked to UNlock,
 return SuccessfulAction = TRUE.
 NOTE:  There are 10 lock locations set aside for the hardware's use.
 It is assumed that the hardware will have initialized these locks
 early on so that they don't interfere with this mechanism.
 *************************************************************************/

void Z502MemoryReadModify(INT32 VirtualAddress, INT32 NewLockValue,
		INT32 Suspend, INT32 *SuccessfulAction) {
	int WhichRecord;
	// GetLock( HardwareLock, "Z502_READ_MODIFY" );   JB - 7/26/06
	if (VirtualAddress < MEMORY_INTERLOCK_BASE
			|| VirtualAddress
					>= MEMORY_INTERLOCK_BASE + MEMORY_INTERLOCK_SIZE + 10
			|| (NewLockValue != 0 && NewLockValue != 1)
			|| (Suspend != TRUE && Suspend != FALSE )) {
		*SuccessfulAction = FALSE;
		return;
	}
	WhichRecord = VirtualAddress - MEMORY_INTERLOCK_BASE + 10;
	if (InterlockRecord[WhichRecord] == -1)
		CreateLock(&(InterlockRecord[WhichRecord]), "Z502MemoryReadModify");
	if (NewLockValue == 1 && Suspend == FALSE)
		*SuccessfulAction = GetTryLock(InterlockRecord[WhichRecord],
				"Z502MemReadMod");
	if (NewLockValue == 1 && Suspend == TRUE) {
		*SuccessfulAction = GetLock(InterlockRecord[WhichRecord],
				"Z502_READ_MODIFY");
	}
	if (NewLockValue == 0) {
		*SuccessfulAction = ReleaseLock(InterlockRecord[WhichRecord],
				"Z502_READ_MODIFY");
	}
	// ReleaseLock( HardwareLock, "Z502_READ_MODIFY" );   JB - 7/26/06

}             // End  Z502MemoryReadModify

/*************************************************************************
 MemoryMappedIO

 We talk to devices via certain memory addresses found in memory
 hyperspace.  In other words, these memory addresses don't point to
 real physical memory.  You must be privileged to touch this hardware.

 *************************************************************************/

void MemoryMappedIO(INT32 address, MEMORY_MAPPED_IO *mmio, BOOL ReadOrWrite) {

	//MEMORY_MAPPED_IO mmio = (MEMORY_MAPPED_IO ) Data;
	INT32 index;
	INT32 Temporary;
	long LongTemporary;

	// We assume that Memory Common has set the Hardware Lock - we don't
	// want to do it again.

	// GetLock ( HardwareLock, "memory_mapped_io" );
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}
	ChargeTimeAndCheckEvents(COST_OF_MEMORY_MAPPED_IO);

	// Switch off the IO Function that the user has defined when
	// doing the memory request
	switch (address) {

	// There's only one action here.  Don't worry about the user packet
	case Z502Halt: {
		if (ReadOrWrite == SYSNUM_MEM_WRITE) {
			HaltSimulation();
		} else {
			mmio->Field4 = ERR_BAD_PARAM;
		}
		break;
	}

		// There's only one action here.  Don't worry about the user packet
	case Z502Idle: {
		if (ReadOrWrite == SYSNUM_MEM_WRITE) {
			IdleSimulation();
		} else {
			mmio->Field4 = ERR_BAD_PARAM;
		}
		break;
	}

	case Z502InterruptDevice: {
		// We want to get the device ID and status
		mmio->Field4 = ERR_SUCCESS;       // Assume success
		Temporary = 0;                    // Used as flag to show action found
		if ((ReadOrWrite == SYSNUM_MEM_READ)
				&& (mmio->Mode == Z502GetInterruptInfo)) {
			mmio->Field1 = -1;
			Temporary = 1;
			// We want to find the target device and assure that it was activated by
			// the thread that's now looking for it.  This means that if a thread
			// took a memory fault, that it will be THAT thread in the OS fault
			// handler that will pull this page number.  NOTE that the interrupts
			// should be single threaded - there's only one interrupt thread that
			// put things on and off the STAT_VECTOR.  But there are many fault
			// threads - one for each of the active processes - so we need to
			// do the disambiguation here.
			for (index = 0; index <= LARGEST_STAT_VECTOR_INDEX; index++) {
				if ((STAT_VECTOR[SV_ACTIVE ][index] != 0)
						&& (STAT_VECTOR[SV_TID ][index] == GetMyTid())) {
					mmio->Field1 = index;                         // Device ID
					mmio->Field2 = STAT_VECTOR[SV_VALUE ][index]; // Device Status
					STAT_VECTOR[SV_VALUE ][index] = 0;  // Invalidate the record
					STAT_VECTOR[SV_ACTIVE ][index] = 0;
					STAT_VECTOR[SV_TID ][index] = 0;
					break;
				}
			}  // end of for
			   // No device was found - report it as an error
			if (mmio->Field1 == -1) {
				mmio->Field4 = ERR_NO_DEVICE_FOUND;
			}
			break;
		}
		// We want to clear the interrupt status of the device we were working with
		// The code for Z502ClearInterruptStatus was removed in Rev 4.40
		if (Temporary == 0) {
			printf(
					"PANIC #1 - Illegal use of parameters in Z502InterruptDevice\n");
			mmio->Field4 = ERR_BAD_PARAM;
		}
		break;
	}

	case Z502Clock: {
		HardwareClock(&Temporary);
		mmio->Field1 = Temporary;
		break;
	}

	case Z502Timer: {
		// Set the timer - the number of time units to delay
		if (mmio->Mode == Z502Start) {
			HardwareTimer(mmio->Field1);
			break;
		}
		// Get the status of the timer
		if (mmio->Mode == Z502Status) {
			if (timer_state.timer_in_use == TRUE)
				mmio->Field1 = DEVICE_IN_USE;
			else
				mmio->Field1 = DEVICE_FREE;
			break;
		}
		mmio->Field4 = ERR_BAD_PARAM;
		printf("PANIC #2 - Illegal use of Memory Mapped IO\n");
		break;
	}    // End of case Z502Timer

	// Do the various operations required for the disk
	case Z502Disk: {
		mmio->Field4 = 0;
		// Check for Status mode first
		if (mmio->Mode == Z502Status) {
			if (mmio->Field1 >= 0 && mmio->Field1 < MAX_NUMBER_OF_DISKS) {
				if (DiskState[mmio->Field1].DiskInUse == TRUE)
					mmio->Field2 = DEVICE_IN_USE;
				else
					mmio->Field2 = DEVICE_FREE;
			} else {
				mmio->Field4 = ERR_BAD_DEVICE_ID;  //Not a legal disk number
			}
			break;
		}    // End of Mode == Status
		// It's not status, do Read/Write/Check Disk
		if ((mmio->Field1 >= 0 && mmio->Field1 < MAX_NUMBER_OF_DISKS )
				&& (mmio->Field2 >= 0 && mmio->Field2 <= NUMBER_LOGICAL_SECTORS )) {
			if (mmio->Mode == Z502DiskRead) {
				HardwareReadDisk((INT16) mmio->Field1, mmio->Field2,
						(char *) mmio->Field3);
			}
			if (mmio->Mode == Z502DiskWrite) {
				HardwareWriteDisk((INT16) mmio->Field1, mmio->Field2,
						(char *) mmio->Field3);
			}
			if (mmio->Mode == Z502CheckDisk) {
				HardwareCheckDisk(mmio->Field1);
			}
			break;
		}
		// We get here only if there's a confusion
		if (DO_DEVICE_DEBUG) {
			printf("------ BEGIN DO_DEVICE DEBUG - IN Z502Disk ----- \n");
			printf("ERROR:  Invalid parameter in command from OS\n");
			printf("-------- END DO_DEVICE DEBUG - ------------------\n");
		}
		mmio->Field4 = ERR_BAD_PARAM;
		break;
	}      // End of case == Z502Disk

		// Implement the Context Hardware Instructions
		// This includes initializing and creating the context, and then
		// starting up that context.
	case Z502Context: {
		if (mmio->Mode == Z502StartContext) {
			ReleaseLock(HardwareLock, "StartContext");
			// We only implement the mode of suspending the caller
			SwitchContext((void *) &mmio->Field1, mmio->Field2);
			GetLock(HardwareLock, "StartContext");
			mmio->Field4 = ERR_SUCCESS;    // Error code
			break;
		}  // End of Mode === StartContext

		if (mmio->Mode == Z502InitializeContext) {
			MakeContext(&LongTemporary, mmio->Field2, (UINT16 *) mmio->Field3,
			KERNEL_MODE);
			mmio->Field1 = LongTemporary;    // Context pointer
			mmio->Field4 = ERR_SUCCESS;      // Error code
			break;
		}  // End of Mode == InitializeContext

		if (mmio->Mode == Z502GetPageTable) {
			mmio->Field1 = (long) GetPageTableAddress();
			mmio->Field4 = ERR_SUCCESS;      // Error code
			break;
		}    // End of Mode == Z502GetPageTable

		if (mmio->Mode == Z502GetCurrentContext) {
			long *abc = (long *) GetCurrentContext();
			mmio->Field1 = (long) abc;
			mmio->Field4 = ERR_SUCCESS;      // Error code
			break;
		}    // End of Mode == Z502GetCurrentContext

		mmio->Field4 = ERR_BAD_PARAM;    // Couldn't handle the mode
		if (DO_DEVICE_DEBUG) {
			printf("- BEGIN DO_DEVICE DEBUG - Z502Context - \n");
			printf("ERROR: Illegal Mode in Context memory mapped IO\n");
			printf("-------- END DO_DEVICE DEBUG - ----------\n");
		}

		break;
	}   // End of case Context

		// Implement a multiprocessor simulation
	case Z502Processor: {
		mmio->Field4 = ERR_SUCCESS;
		if (mmio->Mode == Z502SetProcessorNumber) {
			if (mmio->Field1 > MAX_NUMBER_OF_PROCESSORS) {
				mmio->Field4 = ERR_BAD_PARAM;
			} else {
				Z502_CURRENT_NUMBER_OF_PROCESSORS = mmio->Field1;
			}
			break;
		}    // End of Z502SetProcessorNumber

		// The returned variable was set to 1 when declared.
		// It will only be greater than 1 when the SetProcessorNumber
		//    call has occurred.

		if (mmio->Mode == Z502GetProcessorNumber) {
			mmio->Field1 = Z502_CURRENT_NUMBER_OF_PROCESSORS;
		}		// End of Z502SetProcessorNumber
		break;
	}     // End of case Z502Processor
	default: {   // Bad news if we get here - we don't know how to handle it!
		mmio->Field4 = ERR_BAD_PARAM;
		break;

	}
	} // End of switch

} // End MemoryMappedIO

/*****************************************************************
 PhysicalMemoryCommon

 This code is designed to let an operating system touch physical
 memory - in fact if a user tries to enter this routine, a
 fault occurs.

 The routine reads or writes an entire page of physical memory
 from or to a buffer containing PGSIZE number of bytes.

 This allows the OS to do physical memory accesses without worrying
 about the page table.
 *****************************************************************/

void PhysicalMemoryCommon(INT32 PhysicalPageNumber, char *data_ptr,
		BOOL read_or_write) {
	INT16 PhysicalPageAddress;
	INT16 index;
	char Debug_Text[32];

	strcpy(Debug_Text, "PhysicalMemoryCommon");
	GetLock(HardwareLock, Debug_Text);

	// If a user tries to do this call from user mode, a fault occurs
	if (GetMode() != KERNEL_MODE) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}
	// If the user has asked for an illegal physical page, take a fault
	// then return with no modification to the user's buffer.
	if (PhysicalPageNumber < 0 || PhysicalPageNumber > NUMBER_PHYSICAL_PAGES) {
		ReleaseLock(HardwareLock, Debug_Text);
		HardwareFault(INVALID_PHYSICAL_MEMORY, PhysicalPageNumber);
		return;
	}
	PhysicalPageAddress = PGSIZE * PhysicalPageNumber;

	if (read_or_write == SYSNUM_MEM_READ) {
		for (index = 0; index < PGSIZE ; index++)
			data_ptr[index] = MEMORY[PhysicalPageAddress + index];
	}

	if (read_or_write == SYSNUM_MEM_WRITE) {
		for (index = 0; index < PGSIZE ; index++)
			MEMORY[PhysicalPageAddress + index] = data_ptr[index];
	}

	ChargeTimeAndCheckEvents(COST_OF_MEMORY_ACCESS);
	ReleaseLock(HardwareLock, Debug_Text);
}                      // End of PhysicalMemoryCommon

/*****************************************************************
 Z502ReadPhysicalMemory and  Z502WritePhysicalMemory

 This code reads physical memory.  It doesn't matter what you have
 the page table set to, this code will ignore the page table and
 read/write the physical memory.

 *****************************************************************/
void Z502ReadPhysicalMemory(INT32 PhysicalPageNumber, char *PhysicalDataPointer) {

	PhysicalMemoryCommon(PhysicalPageNumber, PhysicalDataPointer,
			(BOOL) SYSNUM_MEM_READ);
}                  // End  Z502ReadPhysicalMemory

void Z502WritePhysicalMemory(INT32 PhysicalPageNumber,
		char *PhysicalDataPointer) {

	PhysicalMemoryCommon(PhysicalPageNumber, PhysicalDataPointer,
			(BOOL) SYSNUM_MEM_WRITE);
}                  // End  Z502WritePhysicalMemory

/*************************************************************************
 HardwareReadDisk

 This code simulates a disk read.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Do range check on disk_id, sector; give
 interrupt error = ERR_BAD_PARAM if illegal.
 o If an event for this disk already exists ( the disk
 is already busy ), then give interrupt error ERR_DISK_IN_USE.
 o Search for sector structure off of hashed value.
 o If search fails give interrupt error = ERR_NO_PREVIOUS_WRITE
 o Copy data from sector to buffer.
 o From DiskState information, determine how long this request will take.
 o Request a future interrupt for this event.
 o Advance time and see if an interrupt has occurred.

 **************************************************************************/
void HardwareReadDisk(INT16 disk_id, INT16 sector, char *buffer_ptr) {
	INT32 local_error;
	char *sector_ptr = 0;
	INT32 access_time;
	INT16 error_found;

	error_found = 0;
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}
	// This SHOULD have been checked in MemoryMappedIO
	if (disk_id < 0 || disk_id >= MAX_NUMBER_OF_DISKS) {
		disk_id = 0; /* To aim at legal vector  */
		error_found = ERR_BAD_PARAM;
	}

	if (sector < 0 || sector >= NUMBER_LOGICAL_SECTORS)
		error_found = ERR_BAD_PARAM;

	if (error_found == 0) {
		GetSectorStructure(disk_id, sector, &sector_ptr, &local_error);
		if (local_error != 0)
			error_found = ERR_NO_PREVIOUS_WRITE;

		if (DiskState[disk_id].DiskInUse == TRUE)
			error_found = ERR_DISK_IN_USE;
	}

	/* If we found an error, add an event that will cause an immediate
	 hardware interrupt.                                              */

	if (error_found != 0) {
		if (DO_DEVICE_DEBUG) {
			printf("--- BEGIN DO_DEVICE DEBUG - IN read_disk ----- \n");
			printf("ERROR:  Something screwed up   The error\n");
			printf("      code is %d that you can look up in global.h\n",
					error_found);
			printf("     The disk will cause an interrupt to tell \n");
			printf("      you about that error.\n");
			printf("--- END DO_DEVICE DEBUG - ---------------------\n");
		}
		AddEventToInterruptQueue(CurrentSimulationTime,
				(INT16) (DISK_INTERRUPT + disk_id), error_found,
				&DiskState[disk_id].EventPtr);
	} else {
		//memcpy(buffer_ptr, sector_ptr, PGSIZE);   // Bugfix 07/2014
		DiskState[disk_id].Destination = buffer_ptr;
		DiskState[disk_id].Source = sector_ptr;
		access_time = CurrentSimulationTime + 100
				+ abs(DiskState[disk_id].LastSector - sector) / 20;
		HardwareStats.disk_reads[disk_id]++;
		HardwareStats.time_disk_busy[disk_id] += access_time
				- CurrentSimulationTime;
		if (DO_DEVICE_DEBUG) {
			printf("\nDEVICE_DEBUG: Time = %d: ", CurrentSimulationTime);
			printf("  Disk %d READ will interrupt at time = %d\n", disk_id,
					access_time);
		}
		AddEventToInterruptQueue(access_time,
				(INT16) (DISK_INTERRUPT + disk_id), (INT16) ERR_SUCCESS,
				&DiskState[disk_id].EventPtr);
		DiskState[disk_id].LastSector = sector;
	}
	DiskState[disk_id].DiskInUse = TRUE;
	// printf("1. Setting %d TRUE\n", disk_id );
	ChargeTimeAndCheckEvents(COST_OF_DISK_ACCESS);

}               // End of HardwareReadDisk

/*****************************************************************
 HardwareWriteDisk

 This code simulates a disk write.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Do range check on disk_id, sector; give interrupt error
 = ERR_BAD_PARAM if illegal.
 o If an event for this disk already exists ( the disk is already busy ),
 then give interrupt error ERR_DISK_IN_USE.
 o Search for sector structure off of hashed value.
 o If search fails give create a sector on the simulated disk.
 o Copy data from buffer to sector.
 o From DiskState information, determine how long this request will take.
 o Request a future interrupt for this event.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/
void HardwareWriteDisk(INT16 disk_id, INT16 sector, char *buffer_ptr) {
	INT32 local_error;
	char *sector_ptr;
	INT32 access_time;
	INT16 error_found;

	error_found = 0;
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}

	if (disk_id < 0 || disk_id >= MAX_NUMBER_OF_DISKS) {
		disk_id = 1; /* To aim at legal vector  */
		error_found = ERR_BAD_PARAM;
	}
	if (sector < 0 || sector >= NUMBER_LOGICAL_SECTORS)
		error_found = ERR_BAD_PARAM;

	if (DiskState[disk_id].DiskInUse == TRUE)
		error_found = ERR_DISK_IN_USE;

	if (error_found != 0) {
		if (DO_DEVICE_DEBUG) {
			printf("---- BEGIN DO_DEVICE DEBUG - IN write_disk --- \n");
			printf("ERROR:  in your disk request.  The error\n");
			printf("     code is %d that you can look up in global.h\n",
					error_found);
			printf("    The disk will cause an interrupt to tell \n");
			printf("     you about that error.\n");
			printf("---- END DO_DEVICE DEBUG - --------------------\n");
		}
		AddEventToInterruptQueue(CurrentSimulationTime,
				(INT16) (DISK_INTERRUPT + disk_id - 1), error_found,
				&DiskState[disk_id].EventPtr);
	} else {
		GetSectorStructure(disk_id, sector, &sector_ptr, &local_error);
		if (local_error != 0) /* No structure for this sector exists */
			CreateSectorStruct(disk_id, sector, &sector_ptr);

		//memcpy(sector_ptr, buffer_ptr, PGSIZE); // Bugfix 07/2014
		DiskState[disk_id].Destination = sector_ptr;
		DiskState[disk_id].Source = buffer_ptr;

		access_time = (INT32) CurrentSimulationTime + 100
				+ abs(DiskState[disk_id].LastSector - sector) / 20;
		HardwareStats.disk_writes[disk_id]++;
		HardwareStats.time_disk_busy[disk_id] += access_time
				- CurrentSimulationTime;
		if (DO_DEVICE_DEBUG) {
			printf("\nDEVICE_DEBUG: Time = %d:  ", CurrentSimulationTime);
			printf("Disk %d WRITE will cause interrupt at time = %d\n", disk_id,
					access_time);
		}
		AddEventToInterruptQueue(access_time,
				(INT16) (DISK_INTERRUPT + disk_id), (INT16) ERR_SUCCESS,
				&DiskState[disk_id].EventPtr);
		DiskState[disk_id].LastSector = sector;
	}
	// No matter if the disk request succeeds or fails, the disk is set as busy
	DiskState[disk_id].DiskInUse = TRUE;
	// printf("2. Setting %d TRUE\n", disk_id );
	ChargeTimeAndCheckEvents(COST_OF_DISK_ACCESS);

}                           // End of HardwareWriteDisk

/*****************************************************************
 HardwareCheckDisk()

 This code added so we can print out the contents of a formatted
 disk.
 This enables us to check that the disk is correctly set up.
 We assume before we get here someone has checked the validity
 of the disk
 This code was written for Rev 4.30 in February 2016
 *****************************************************************/
void HardwareCheckDisk(int DiskID) {
	FILE *Output;
	int Index, Index2;
	int Result;
	INT32 local_error;
	char *BufferPointer;
	unsigned char LocalBuffer[PGSIZE ];
	char OutputString[120];
	char TempString[16];

	Output = fopen("CheckDiskData", "w");
	for (Index = 0; Index < NUMBER_LOGICAL_SECTORS ; Index++) {
		//	HardwareReadDisk(DiskID, Index, &BufferPointer);
		GetSectorStructure(DiskID, Index, &BufferPointer, &local_error);
		if (local_error == 0) {      // it's a good sector
			memcpy(LocalBuffer, BufferPointer, PGSIZE);
			// Determine if the Sector contains all zeros.  If so, don't print.
			Result = 0;
			for (Index2 = 0; Index2 < PGSIZE ; Index2++) {
				Result += LocalBuffer[Index2];
			}
			if (Result > 0) {
				sprintf(TempString, "%04X ", Index);
				TempString[5] = '\0';
				memcpy(OutputString, TempString, 6);
				for (Index2 = 0; Index2 < PGSIZE ; Index2++) {
					sprintf(TempString, "%02X ", LocalBuffer[Index2]);
					strncat(OutputString, TempString, 3);
				}
				printf("%s\n", OutputString);
				fprintf(Output, "%s\n", OutputString);
			}
		}
	}
	fclose(Output);
}              // End HardwareCheckDisk
/*****************************************************************

 HardwareTimer()

 This is the routine that sets up a clock to interrupt
 in the future.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o If time is illegal, generate an interrupt immediately.
 o Purge any outstanding timer events.
 o Request a future event.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/

void HardwareTimer(INT32 time_to_delay) {
	INT32 error;
	UINT32 CurrentTimerExpirationTime = 9999999;

	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}

	// Bugfix 4.0 - if the time is 0 on Linux, the interrupt may occur
	// too soon.  We really need a lock here and on HardwareInterrupt
	if (time_to_delay == 0)
		time_to_delay = 1;

	if (DO_DEVICE_DEBUG) {           // Print lots of info
		printf("\nDEVICE_DEBUG: HardwareTimer:  ");
		if (timer_state.timer_in_use == TRUE) {
			printf("The timer is ALREADY in use - ");
			printf("destroying previous request\n");
		} else
			printf("The timer is not currently running\n");
		if (time_to_delay < 0)
			printf(
					"DEVICE_DEBUG: TROUBLE - want to delay for negative time!!\n");
		if (timer_state.timer_in_use == TRUE)
			CurrentTimerExpirationTime = timer_state.event_ptr->time_of_event;
		if (CurrentTimerExpirationTime
				< CurrentSimulationTime + time_to_delay) {
			printf(
					"DEVICE_DEBUG: TROUBLE - you are replacing the current timer ");
			printf("value of %d with a time of %d\n",
					CurrentTimerExpirationTime,
					CurrentSimulationTime + time_to_delay);
			printf(
					"DEVICE_DEBUG: which is even further in the future.  This is not wise\n");
		}
		printf(
				"DEVICE_DEBUG:    Time Now = %d, Delay time = %d, Interrupt will occur at = %d\n",
				CurrentSimulationTime, time_to_delay,
				CurrentSimulationTime + time_to_delay);
	}     // End of if DO_DEVICE_DEBUG

	if (timer_state.timer_in_use == TRUE) {
		DequeueItemFromEventQueue(timer_state.event_ptr, &error);
		if (error != 0) {
			printf("Internal error - we tried to retrieve a timer\n");
			printf("event, but failed in HardwareTimer.\n");
			HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
		}
		timer_state.timer_in_use = FALSE;
	}

	if (time_to_delay < 0) {   // Illegal time
		AddEventToInterruptQueue(CurrentSimulationTime, TIMER_INTERRUPT,
				(INT16) ERR_BAD_PARAM, &timer_state.event_ptr);

		return;
	}

	AddEventToInterruptQueue(CurrentSimulationTime + time_to_delay,
	TIMER_INTERRUPT, (INT16) ERR_SUCCESS, &timer_state.event_ptr);
	timer_state.timer_in_use = TRUE;
	ChargeTimeAndCheckEvents(COST_OF_TIMER);

}                                       // End of HardwareTimer

/*****************************************************************

 HardwareClock()

 This is the routine that makes the current simulation
 time visible to the OS502.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Read the simulation time from
 "CurrentSimulationTime"
 o Return it to the caller.

 *****************************************************************/

void HardwareClock(INT32 *current_time_returned) {
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		*current_time_returned = -1; /* return bogus value      */
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}

	ChargeTimeAndCheckEvents(COST_OF_CLOCK);
	*current_time_returned = (INT32) CurrentSimulationTime;

}           // End of HardwareClock

/*****************************************************************
 HaltSimulation()

 This is the routine that ends the simulation.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Wrapup any outstanding work and terminate.

 *****************************************************************/

void HaltSimulation(void) {
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}
	PrintHardwareStats();

	printf("The Z502 halts execution and Ends at Time %d\n",
			CurrentSimulationTime);
	GoToExit(0);
}                     // End of Z502Halt

/*****************************************************************
 IdleSimulation()

 This is the routine that idles the Z502 until the next
 interrupt.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o If there's nothing to wait for, print a message
 and halt the machine.
 o Get the next event and cause an interrupt.

 *****************************************************************/

void IdleSimulation(void) {
	INT32 time_of_next_event;
	static INT32 NumberOfIdlesWithNothingOnEventQueue = 0;

	GetLock(HardwareLock, "Z502Simulation");

	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}

	GetNextEventTime(&time_of_next_event);
	if (DO_DEVICE_DEBUG) {
		printf("\nDEVICE_DEBUG: Z502Simulation: Time is = %d: ",
				CurrentSimulationTime);
		if (time_of_next_event < 0)
			printf("There's no event enqueued - timer not started\n");
		else
			printf("Starting Timer for future time = %d\n", time_of_next_event);
	}
	if (time_of_next_event < 0)
		NumberOfIdlesWithNothingOnEventQueue++;
	else
		NumberOfIdlesWithNothingOnEventQueue = 0;
	if (NumberOfIdlesWithNothingOnEventQueue > 20) {
		printf("ERROR in Z502Idle.  IDLE will wait forever since\n");
		printf("there is no event that will cause an interrupt.\n");
		printf("This occurs because when you decided to call 'Z502Idle'\n");
		printf("there was an event waiting - but the event was triggered\n");
		printf("too soon. Avoid this error by:\n");
		printf("1. using   'ZCALL' instead of CALL for all routines between\n");
		printf("   the event-check and Z502Idle\n");
		printf("2. limiting the work or routines (like printouts) between\n");
		printf("   the event-check and Z502Idle\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	if ((time_of_next_event > 0)
			&& (CurrentSimulationTime < (UINT32) time_of_next_event))
		CurrentSimulationTime = time_of_next_event;
	ReleaseLock(HardwareLock, "Z502Simulation");
	SignalCondition(InterruptCondition, "Z502Simulation");
}                    // End of Z502Idle

/*****************************************************************

 MakeContext()

 This is the routine that sets up a new context.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Allocate a structure for a context.  The "calloc" sets the contents
 of this memory to 0.
 o Ensure that memory was actually obtained.
 o Initialize the structure.
 o Associate the Context with a thread that will run it
 o Advance time and see if an interrupt has occurred.
 o Return the structure pointer to the caller.

 *****************************************************************/

void MakeContext(long *ReturningContextPointer, long starting_address,
		UINT16* PageTable, BOOL user_or_kernel) {
	Z502CONTEXT *our_ptr;
	int Temporary;

	if (Z502Initialized == FALSE) {
		Z502Init();
	}

	GetLock(HardwareLock, "Z502MakeContext");
	// We need to be in kernel mode or be in interrupt handler
	if (GetMode() != KERNEL_MODE && InterruptTid != GetMyTid()) {
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}

	our_ptr = (Z502CONTEXT *) calloc(1, sizeof(Z502CONTEXT));
	if (our_ptr == NULL) {
		printf("We didn't complete the calloc in MAKE_CONTEXT.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}

	// Our goal here is to save the OS developer some pain later on.
	// We assume that the page table handed to us is valid, and that it
	// has a length of VIRTUAL_MEM_PAGES.  Check that we can touch this
	// much memory.  If not, then we will crash here rather than later.
	Temporary = PageTable[0];
	PageTable[NUMBER_VIRTUAL_PAGES - 1] = Temporary;
	// Well, if we get here, then the OS correctly allocated memory.

	our_ptr->StructureID = CONTEXT_STRUCTURE_ID;
	our_ptr->CodeEntry = (void *) starting_address;
	our_ptr->PageTablePointer = (void *) PageTable;
	our_ptr->ContextStartCount = 0;
	// our_ptr->program_mode = user_or_kernel;    BUGFIX  4.10 - July 2014
	our_ptr->ProgramMode = KERNEL_MODE;  // Always start process in Kernel Mode
	our_ptr->FaultInProgress = FALSE;
	*ReturningContextPointer = (long) our_ptr;

	// Attach the Context to a thread
	AssociateContextWithProcess(our_ptr);

	ChargeTimeAndCheckEvents(COST_OF_MAKE_CONTEXT);
	ReleaseLock(HardwareLock, "Z502MakeContext");

}                    // End of MakeContext

/*****************************************************************

 Z502DestroyContext()

 This is the routine that removes a context.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Validate structure_id on context.  If bogus, return
 fault error = ERR_ILLEGAL_ADDRESS.
 o Free the memory pointed to by the pointer.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/
/*
 void Z502DestroyContext(void **IncomingContextPointer) {
 Z502CONTEXT **context_ptr = (Z502CONTEXT **) IncomingContextPointer;

 GetLock(HardwareLock, "Z502DestroyContext");
 // We need to be in kernel mode or be in interrupt handler
 if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
 HardwareFault(PRIVILEGED_INSTRUCTION, 0);
 return;
 }

 if (GetCurrentContext() != NULL &&
 GetCurrentContext()->structure_id != CONTEXT_STRUCTURE_ID) {
 printf("PANIC:  Attempt to destroy context of the currently ");
 printf("running process.\n");
 HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
 }

 if ((*context_ptr)->structure_id != CONTEXT_STRUCTURE_ID)
 HardwareFault(CPU_ERROR, (INT16) ERR_ILLEGAL_ADDRESS);

 (*context_ptr)->structure_id = 0;
 free(*context_ptr);
 ReleaseLock(HardwareLock, "Z502DestroyContext");

 }                   // End of Z502DestroyContext
 */
/*****************************************************************

 SwitchContext()

 This is the routine that sets up a new context.
 Actions include:
 DoStartSuspend must have one of these values:
 SUSPEND_CURRENT_CONTEXT_ONLY  - Suspend the current context but don't start anyone
 START_NEW_CONTEXT_ONLY        - Start the context but don't suspend
 START_NEW_CONTEXT_AND_SUSPEND - Both start the context AND suspend

 o If not in KERNEL_MODE, then cause priv inst trap.
 o Validate StructureID on context.  If bogus, return
 fault error = ERR_ILLEGAL_ADDRESS.
 o Validate kill_or_save parameter.  If bogus, return
 fault error = ERR_BAD_PARAM.
 n Save the context in a well known place so the hardware
 always knows who's running.   (in Z502_CURRENT_CONTEXT)
 o If "next_context_ptr" is null, it means we only want to suspend ourselves
 o Move stuff from new context to registers.
 o If this context took a memory fault, that fault
 is unresolved.  Instead of going back to the user
 context, try the memory reference again.
 o Call the starting address.

 *****************************************************************/

void SwitchContext(void **IncomingContextPointer, BOOL DoStartSuspend) {
	Z502CONTEXT *NextContextPtr;      // The context we will be running on
	Z502CONTEXT *CallersPtr;    // The context we're CURRENTLY running on
	Z502CONTEXT **TargetContextPtr = (Z502CONTEXT **) IncomingContextPointer;
	int ourLocalID = -1;
	int i;

	GetLock(HardwareLock, "SwitchContext");

	// We need to be in kernel mode or be in interrupt handler
	if ((GetMode() != KERNEL_MODE ) && (InterruptTid != GetMyTid())) {
		ReleaseLock(HardwareLock, "SwitchContext");
		HardwareFault(PRIVILEGED_INSTRUCTION, 0);
		return;
	}
	// If we are simply suspending ourselves, we won't have a new context here.
	// So the incoming context provided by the caller may not be valid.
	if (DoStartSuspend != SUSPEND_CURRENT_CONTEXT_ONLY) {
		if ((*TargetContextPtr)->StructureID != CONTEXT_STRUCTURE_ID) {
			ReleaseLock(HardwareLock, "SwitchContext");
			HardwareFault(CPU_ERROR, (INT16) ERR_ILLEGAL_ADDRESS);
		}
		// Find target Context in the table & make sure all is OK
		for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
			if (ThreadTable[i].Context == *TargetContextPtr) {
				ourLocalID = i;
				break;
			}
		}
	}

	//curr_ptr = GetCurrentContext();
	CallersPtr = GetCurrentContext();
	if (DoStartSuspend == SUSPEND_CURRENT_CONTEXT_ONLY) {
		HardwareStats.NumberRunningProcesses--; // One less - suspending ourselves
	}
	if (DoStartSuspend == START_NEW_CONTEXT_ONLY) {
		HardwareStats.NumberRunningProcesses++;   // Starting a thread
	}
	if (DoStartSuspend == START_NEW_CONTEXT_AND_SUSPEND) {
		HardwareStats.NumberRunningProcesses++;   // Starting a thread
		HardwareStats.NumberRunningProcesses--; // One less - suspending ourselves
	}
	HardwareStats.TimeWeightedNumberRunningProcesses +=
			HardwareStats.NumberRunningProcesses;
	HardwareStats.context_switches++;

	// If we're switching to the same thread, then we could have a problem
	// because we are resuming ourselves (not suspended!) and then suspending
	// ourselves which will cause us to hang -- it does so on LINUX.
	// BUT make sure that the suspend is the last instruction in this routine
	//    and we don't do any work after it.
	//  printf("Z502Switch... curr = %lX, Incoming = %lX\n",
	//         (unsigned long)curr_ptr, (unsigned long)*context_ptr);
	if (CallersPtr == *TargetContextPtr) {
		ReleaseLock(HardwareLock, "SwitchContext");
		//      printf("Z502Switch... - returning with no switch\n");
		return;
	}
	// This could be the first context we want to run, so the
	// CURRENT_CONTEXT might be NULL.
	// But otherwise, make sure we associate a mode and a page table
	// to this process.
	if (GetCurrentContext() != NULL) {
		if (CallersPtr->StructureID != CONTEXT_STRUCTURE_ID) {
			printf("CURRENT_CONTEXT is invalid in SwitchContext\n");
			printf("The hardware is reporting this immediately rather");
			printf("returning an error so you can fix it knowing");
			printf("where the bad pointer is generated.");
			HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
		}
		CallersPtr->ProgramMode = GetMode(); // Added Bugfix 12/8/13
		CallersPtr->PageTablePointer = GetPageTableAddress();

	}                           // End of current context not null

	//  NOW we will be working with the NEXT context
	NextContextPtr = *TargetContextPtr;    // This will be the NEW context

	// 6/25/13 - not clear that this works as it should.  Verify that the
	//   interruptTid is actually getting set.
	if (InterruptTid == GetMyTid()) { // Are we running on hardware thread
		printf("Trying to switch context while at interrupt level > 0.\n");
		printf("This is NOT advisable and will lead to strange results.\n");
	}

	// Go wake up the target thread.  If it's a first time schedule for this
	// thread, it will start up in the Z502PrepareProcessForExecution
	// code.  Otherwise it will continue down at the bottom of this routine.
	if (DoStartSuspend == START_NEW_CONTEXT_ONLY) {
		ResumeProcessExecution(NextContextPtr);
		// OK - we're free to unlock our work here - it's done.
		ReleaseLock(HardwareLock, "SwitchContext");
		return;
	}
	// We're going to do both the start AND the suspend
	if (DoStartSuspend == START_NEW_CONTEXT_AND_SUSPEND) {
		ResumeProcessExecution(NextContextPtr);
	}

	// Go suspend the original thread - the one that called SwitchContext
	// That means when this thread is later awakened, it will resume
	// execution at THIS point and will return from SwitchContext.

	// Caller has COMPLETE control of whether to suspend or not.
	// This decision will be based on caller's knowledge of the number of
	// processors and scheduling policy.
	ReleaseLock(HardwareLock, "SwitchContext");
	ThreadTable[ourLocalID].CurrentState = SUSPENDED_AFTER_BEING_ACTIVE;
	SuspendProcessExecution(CallersPtr);
	ThreadTable[ourLocalID].CurrentState = ACTIVE;

	//  MAKE SURE NO SIGNIFICANT WORK IS INSERTED AT THIS POINT

	// Release 4.0 - we come to this point when we are Resumed by a process.
}                                    // End of SwitchContext

/*****************************************************************
 * Get the Process Properties Associated With This Processor
 * GetProcessorID() - Function returns the ID of the processor
 *       from which this call was made.
 * GetCurrentContext() - Function returns address of Contextvoid SwitchContextWithSuspend(void **IncomingContextPointer, BOOL DoSuspend) {
 *       associated with caller.
 * SetCurrentContext()  When a processor is started, place the
 *       context that's being run in a place we can find it.
 * GetPageTableAddress() - Function returns address of Page Table
 *       in use by the caller.
 ****************************************************************/
//
// For right now, everything runs on Processor 0
int GetProcessorID() {
	//if (MULTIPROCESSOR_IMPLEMENTED) {
	int i;
	int myTid = GetMyTid();
	int ourLocalID = -1;

	// Find my TID in the table & make sure all is OK
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].ThreadID == myTid) {
			ourLocalID = i;
			break;
		}
	}
	if (ourLocalID == -1) {
		printf("Error in GetProcessorID\n");
		printf("This should never happen!");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	return ourLocalID; // This is a mapping between our local ID and the processor number
//	}
	//return 0;
}  // End of GetProcessorID

// Return the address of the context of the process that's
//   currently running on the processor of the caller
Z502CONTEXT *GetCurrentContext() {
	return ThreadTable[GetProcessorID()].Context;
}     // End of GetCurrentContext

// Set the context of the process that's currently being
//     run on this processor
void SetCurrentContext(Z502CONTEXT *Address) {
	ThreadTable[GetProcessorID()].Context = Address;
}     // End of SetCurrentContext

// Return the Page Table of the  process that's
//   currently running on the processor of the caller
UINT16 *GetPageTableAddress() {
	return ThreadTable[GetProcessorID()].Context->PageTablePointer;
}    // End of  GetPageTableAddress()

// Sets the Page Table of the  process that's
//   currently running on the processor of the caller
void SetPageTableAddress(UINT16 *address) {
	ThreadTable[GetProcessorID()].Context->PageTablePointer = address;
}    // End of  SetPageTableAddress()

// Gets the MODE of the process that's
//   currently running on the processor of the caller
INT16 GetMode() {
	return ThreadTable[GetProcessorID()].Mode;
}    // End of  GetMode()

// Gets the MODE of the process that's
//   currently running on the processor of the caller
void SetMode(INT16 mode) {
	ThreadTable[GetProcessorID()].Mode = mode;
}    // End of  SetMode()

/*****************************************************************

 ChargeTimeAndCheckEvents()

 This is the routine that will increment the simulation
 clock and then check that no event has occurred.
 Actions include:
 o Increment the clock.
 o IF interrupts are masked, don't even think about
 trying to do an interrupt.
 o If interrupts are NOT masked, determine if an interrupt
 should occur.  If so, then signal the interrupt thread.

 ******************************************************************/

void ChargeTimeAndCheckEvents(INT32 time_to_charge) {
	INT32 time_of_next_event;

	CurrentSimulationTime += time_to_charge;
	HardwareStats.number_charge_times++;

	//printf( "Charge_Time... -- current time = %ld\n", CurrentSimulationTime );
	GetNextEventTime(&time_of_next_event);
	if (time_of_next_event > 0
			&& time_of_next_event <= (INT32) CurrentSimulationTime) {
		SignalCondition(InterruptCondition, "Charge_Time");
	}
}              // End of ChargeTimeAndCheckEvents

/*****************************************************************

 HardwareInterrupt()

 NOTE:  This code runs as a separate thread.
 This is the routine that will cause the hardware interrupt
 and will call OS502.      Actions include:
 o Wait for a signal from base level.
 o Get the next event - we expect the time has expired, but if
 it hasn't do nothing.
 o If it's a device, show that the device is no longer busy.
 o Set up registers which user interrupt handler will see.
 o Call the interrupt handler.

 Simply return if no event can be found.
 *****************************************************************/

void HardwareInterrupt(void) {
	INT32 time_of_event;
	INT16 event_type;
	INT16 event_error;
	INT32 local_error;
	INT32 TimeToWaitForCondition = 30; // Millisecs before Condition will go off
	INT32 *DataPointer;
	void (*InterruptHandler)(void);

	InterruptTid = GetMyTid();
	while (TRUE ) {
		GetNextEventTime(&time_of_event);
		while (time_of_event < 0
				|| time_of_event > (INT32) CurrentSimulationTime) {
			GetLock(InterruptLock, "HardwareInterrupt-1");
			WaitForCondition(InterruptCondition, InterruptLock,
					TimeToWaitForCondition, "HardwareInterrupt");
			//ReleaseLock(InterruptLock, "HardwareInterrupt-1");
			GetNextEventTime(&time_of_event);
			// PrintEventQueue( );
			if (DEBUG_CONDITION) {
				printf("Hardware_Interrupt: time = %d: next event = %d\n",
						CurrentSimulationTime, time_of_event);
			}  // End  DEBUG_CONDITION
		}      // End while

		// We got here because there IS an event that needs servicing.

		GetLock(HardwareLock, "HardwareInterrupt-2");
		NumberOfInterruptsStarted++;
		GetNextOrderedEvent(&time_of_event, &event_type, &event_error,
				&local_error);
		if (local_error != 0) {
			printf("In HardwareInterrupt we expected to find an event;\n");
			printf("A timer or disk interrupt we are to act upon.");
			printf("Something in the OS has destroyed this location.\n");
			HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
		}

		if (event_type >= DISK_INTERRUPT
				&& event_type <= DISK_INTERRUPT + MAX_NUMBER_OF_DISKS - 1) {
			/* Note - if we get a disk error, we simply enqueued an event
			 and incremented (hopefully momentarily) the DiskInUse value */
			if (DiskState[event_type - DISK_INTERRUPT ].DiskInUse == FALSE) {
				printf("False interrupt - the Z502 got an interrupt from a\n");
				printf("DISK - but that disk wasn't in use.\n");
				printf("  If you can reproduce this error, please give a\n");
				printf("  copy of your code to your instructor.\n");
				HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
			}

			//  We MAYBE should be clearing all these as well - and not just the current one.
			memcpy(DiskState[event_type - DISK_INTERRUPT ].Destination, // Bugfix 07/2014
					DiskState[event_type - DISK_INTERRUPT ].Source, PGSIZE);
			if (DO_DEVICE_DEBUG) {
				DataPointer =
						(INT32 *) DiskState[event_type - DISK_INTERRUPT ].Source;
				printf(
						"\nDEVICE_DEBUG: HardwareInterrupt - Moving Disk data\n");
				printf("DEVICE_DEBUG:    Source Addr = %lX  ",
						(unsigned long) DiskState[event_type - DISK_INTERRUPT
								+ 1].Source);
				printf("Dest Addr = %lX\n",
						(unsigned long) DiskState[event_type - DISK_INTERRUPT
								+ 1].Destination);
				printf("DEVICE_DEBUG:    Contents = %d  ", DataPointer[0]);
				printf("%d  ", DataPointer[1]);
				printf("%d  ", DataPointer[2]);
				printf("%d\n", DataPointer[3]);
			}
			DiskState[event_type - DISK_INTERRUPT ].DiskInUse = FALSE;
			// printf("3. Setting %d FALSE\n", event_type );
			DiskState[event_type - DISK_INTERRUPT ].EventPtr = NULL;
		}

		if (event_type == TIMER_INTERRUPT && event_error == ERR_SUCCESS) {
			if (timer_state.timer_in_use == FALSE) {
				printf("False interrupt - the Z502 got an interrupt from a\n");
				printf("TIMER - but that timer wasn't in use.\n");
				HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
			}
			timer_state.timer_in_use = FALSE;
			timer_state.event_ptr = NULL;
		}

		/*******************************************************************************
		 * Here we set the STAT_VECTOR.  This conveys to the OS what device and device
		 * characteristics have caused the interrupt.
		 * Here we should see those devices that cause an interrupt - the timer or one
		 * of the disks.
		 * This SHOULD be a single thread - i.e., an event is put in the STAT_VECTOR
		 * here, and then it is read and removed by calls in the OS Interrupt Handler.
		 * This SHOULD be nice and simple, but people report that the are some
		 * devices that don't get interrupted, and there are times that an interrupt
		 * produces no device found.  Don't understand why that would be.
		 * NOTE:  That we set the contents of the record, and the last thing we do
		 *        here is set the flag [SV_VALUE] saying the record is valid.
		 ******************************************************************************/
		STAT_VECTOR[SV_VALUE ][event_type] = event_error;
		STAT_VECTOR[SV_TID ][event_type] = GetMyTid(); // This is Interrupt Thread
		STAT_VECTOR[SV_ACTIVE ][event_type] = 1;

		if (DO_DEVICE_DEBUG) {
			printf("DEVICE_DEBUG: HardwareInterrupt: Time = %d: ",
					CurrentSimulationTime);
			printf("Handling event scheduled for = %d\n", time_of_event);
			printf(
					"DEVICE_DEBUG:     The hardware will now enter your InterruptHandler.\n");
		}

		//  If we've come here from Z502_IDLE, then the current time may be
		// less than the event time. Then we must increase the
		// CurrentSimulationTime to match the time given by the event.
		//
		// if ( ( INT32 )CurrentSimulationTime < time_of_event )
		// CurrentSimulationTime              = time_of_event;
		//
		/*
		 if (GetCurrentContext()->structure_id != CONTEXT_STRUCTURE_ID) {
		 printf("Z502_CURRENT_CONTEXT is invalid in hard_interrupt\n");
		 printf("Something in the OS has destroyed this location.\n");
		 HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
		 }
		 */
		/*******************************************************************************
		 * Here's the magic spot where we call the OS InterruptHandler.
		 ******************************************************************************/
		ReleaseLock(HardwareLock, "HardwareInterrupt-2");

		InterruptHandler =
				(void (*)(void)) TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR ];
		(*InterruptHandler)();

		/* We clean up after returning from the user's interrupt handler */

		GetLock(HardwareLock, "HardwareInterrupt-3"); // I think this is needed
		if ((GetCurrentContext() != NULL)
				&& (GetCurrentContext()->StructureID != CONTEXT_STRUCTURE_ID )) {
			printf("The Z502 hardware has checked to make sure that the  \n");
			printf("value of the context is still valid.  But we find that\n");
			printf("Something in the OS has destroyed the context\n");
			printf("information.\n");
			HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
		}

		ReleaseLock(HardwareLock, "HardwareInterrupt-3");
		NumberOfInterruptsCompleted++;
	}         // End of while TRUE
}                 // End of HardwareInterrupt

/*****************************************************************

 HardwareFault()

 This is the routine that will cause the hardware fault and will call OS502.
 Actions include:
 o Set up the registers which the fault handler will see.
 o Call the fault handler.

 *****************************************************************/
void HardwareFault(INT16 fault_type, INT16 argument) {
	void (*FaultHandler)(void);

	STAT_VECTOR[SV_ACTIVE ][fault_type] = 1;
	STAT_VECTOR[SV_VALUE ][fault_type] = (INT16) argument;
	STAT_VECTOR[SV_TID ][fault_type] = GetMyTid();
	SetMode(KERNEL_MODE);
	HardwareStats.number_faults++;
	FaultHandler = (void (*)(void)) TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR ];

	//  We're about to get out of the hardware - release the lock
	// ReleaseLock( HardwareLock );
	(*FaultHandler)();
	// GetLock( HardwareLock, "HardwareFault" );
	if (GetCurrentContext() != NULL
			&& GetCurrentContext()->StructureID != CONTEXT_STRUCTURE_ID) {
		printf("The Z502 hardware does not understand the context ID.\n");
		printf("  This location is invalid in HardwareFault\n");
		printf("  Something in the OS has destroyed this location.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	SetMode(GetCurrentContext()->ProgramMode);
}              // End of HardwareFault

/*****************************************************************

 SoftwareTrap()

 When a system call occurs, a hardware instruction causes the code
 to enter a known place in the hardware.  That's this location.
 Our main task is to set the mode (Kernel or user) as a way of
 protecting the system from unauthorized access to the hardware.

 This is the routine that will cause the software trap
 and will call OS502.
 Actions include:
 o Set up the registers which the OS trap handler will see.
 o Call the trap_handler.

 *****************************************************************/

void SoftwareTrap(SYSTEM_CALL_DATA *SystemCallData) {
	void (*trap_handler)(SYSTEM_CALL_DATA *);

	SetMode(KERNEL_MODE);
	ChargeTimeAndCheckEvents(COST_OF_SOFTWARE_TRAP);
	trap_handler =
			(void (*)(SYSTEM_CALL_DATA *)) TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR ];
	(*trap_handler)(SystemCallData);
	STAT_VECTOR[SV_ACTIVE ][SOFTWARE_TRAP ] = 0;
	STAT_VECTOR[SV_VALUE ][SOFTWARE_TRAP ] = 0;
	STAT_VECTOR[SV_TID ][SOFTWARE_TRAP ] = 0;
	SetMode(USER_MODE);

}             // End of SoftwareTrap

/*****************************************************************

 HardwareInternalPanic()

 When we come here, it's all over.  The hardware will come
 to a grinding halt.
 Actions include:
 o Print out who caused the problem.
 o Get out of the simulation.

 *****************************************************************/

void HardwareInternalPanic(INT32 panic_type) {
	if (panic_type == ERR_Z502_INTERNAL_BUG) {
		printf("PANIC: Occurred because the simulator could not proceed.\n");
		printf("This most likely occurred because the OS code modified\n");
		printf("data used by the hardware.  This often occurs because the");
		printf("OS writes to a location outside it's normal range.\n");
	}
	if (panic_type == ERR_OS502_GENERATED_BUG)
		printf("PANIC: Because OS502 used hardware wrong.\n");
	PrintRingBuffer();
	GoToExit(0);
}                  // End of HardwareInternalPanic

/*****************************************************************

 AddEventToInterruptQueue()

 This is the routine that will add an event to the queue.
 Actions include:
 o Do lots of sanity checks.
 o Allocate a structure for the event.
 o Fill in the structure.
 o Enqueue it.
 Store data in ring buffer for possible debugging.

 *****************************************************************/

void AddEventToInterruptQueue(INT32 time_of_event, INT16 event_type,
		INT16 event_error, EVENT **returned_event_ptr) {
	EVENT *ep;
	EVENT *temp_ptr;
	EVENT *last_ptr;
	INT16 erbi; /* Short for EventRingBuffer_index    */

	if (time_of_event < (INT32) CurrentSimulationTime) {
		// Bugfix - on Linux there were occurrences of the Simulation
		// time being advanced while a timer request was in progress.

		time_of_event = (INT32) CurrentSimulationTime;
	}
	if (event_type < 0 || event_type > LARGEST_STAT_VECTOR_INDEX) {
		printf("Illegal event_type= %d  in AddEvent.\n", event_type);
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	ep = (EVENT *) malloc(sizeof(EVENT));
	if (ep == NULL) {
		printf("We didn't complete the malloc in AddEvent.\n");
		printf("This should not occur unless the OS has used up\n");
		printf("all the heap space.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	GetLock(EventLock, "AddEvent");

	ep->queue = (INT32 *) NULL;
	ep->time_of_event = time_of_event;
	ep->ring_buffer_location = EventRingBuffer_index;
	ep->structure_id = EVENT_STRUCTURE_ID;
	ep->event_type = event_type;
	ep->event_error = event_error;
	*returned_event_ptr = ep;
	if (DO_DEVICE_DEBUG) {
		printf(
				"In  AddEventToInterruptQueue:  Time Of Event, = %d, Event Type = %d, Event Error = %d\n",
				time_of_event, event_type, event_error);
	}
	erbi = EventRingBuffer_index;
	EventRingBuffer[erbi].time_of_request = CurrentSimulationTime;
	EventRingBuffer[erbi].expected_time_of_event = time_of_event;
	EventRingBuffer[erbi].real_time_of_event = -1;
	EventRingBuffer[erbi].event_type = event_type;
	EventRingBuffer[erbi].event_error = event_error;
	EventRingBuffer_index = (++erbi) % EVENT_RING_BUFFER_SIZE;

	temp_ptr = &EventQueue; /* The header queue  */
	temp_ptr->time_of_event = -1;
	last_ptr = temp_ptr;

	while (1) {
		if (temp_ptr->time_of_event > time_of_event) /* we're past */
		{
			ep->queue = last_ptr->queue;
			last_ptr->queue = (INT32 *) ep;
			break;
		}
		if (temp_ptr->queue == NULL) {
			temp_ptr->queue = (INT32 *) ep;
			break;
		}
		last_ptr = temp_ptr;
		temp_ptr = (EVENT *) temp_ptr->queue;
	} /* End of while     */
	if (ReleaseLock(EventLock, "AddEvent") == FALSE)
		printf("Took error on ReleaseLock in AddEvent\n");
	// PrintEventQueue();
	if ((time_of_event > 0)
			&& (time_of_event <= (INT32) CurrentSimulationTime)) {
		// Bugfix 09/2011 - There are situations where the hardware lock
		// is held in MemoryCommon - but we need to release it so that
		// we can do the signal and the interrupt thread will not be
		// stuck when it tries to get the lock.
		// Here we try to get the lock.  After we try, we will now
		//   hold the lock so we can then release it.
		GetTryLock(HardwareLock, "AddEventToIntQ");
		if (ReleaseLock(HardwareLock, "AddEvent") == FALSE)
			printf("Took error on ReleaseLock in AddEvent\n");
		SignalCondition(InterruptCondition, "AddEvent");
	}
	return;
}             // End of  AddEventToInterruptQueue

/*****************************************************************

 GetNextOrderedEvent()

 This is the routine that will remove an event from the queue.
 Actions include:
 o Gets the next item from the event queue.
 o Fills in the return arguments.
 o Frees the structure.
 We come here only when we KNOW time is past.  We take an error
 if there's nothing on the queue.
 *****************************************************************/

void GetNextOrderedEvent(INT32 *time_of_event, INT16 *event_type,
		INT16 *event_error, INT32 *local_error)

{
	EVENT *ep;
	INT16 rbl; /* Ring Buffer Location                */

	GetLock(EventLock, "get_next_ordered_ev");
	if (EventQueue.queue == NULL) {
		*local_error = ERR_Z502_INTERNAL_BUG;
		if (ReleaseLock(EventLock, "get_next_ordered_ev") == FALSE)
			printf("Took error on ReleaseLock in GetNextOrderedEvent\n");
		return;
	}
	ep = (EVENT *) EventQueue.queue;
	EventQueue.queue = ep->queue;
	ep->queue = NULL;

	if (ep->structure_id != EVENT_STRUCTURE_ID) {
		printf("Bad structure id read in GetNextOrderedEvent.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	*time_of_event = ep->time_of_event;
	*event_type = ep->event_type;
	*event_error = ep->event_error;
	*local_error = ERR_SUCCESS;
	rbl = ep->ring_buffer_location;

	if (EventRingBuffer[rbl].expected_time_of_event == *time_of_event)
		EventRingBuffer[rbl].real_time_of_event = CurrentSimulationTime;

	if (ReleaseLock(EventLock, "GetNextOrderedEvent") == FALSE)
		printf("Took error on ReleaseLock in GetNextOrderedEvent\n");
	ep->structure_id = 0; /* make sure this isn't mistaken */
	free(ep);

}                       // End of GetNextOrderedEvent

/*****************************************************************

 PrintEventQueue()

 Print out the times that are on the event Q.
 *****************************************************************/

void PrintEventQueue() {
	EVENT *ep;

	GetLock(EventLock, "PrintEventQueue");
	printf("Event Queue: ");
	ep = (EVENT *) EventQueue.queue;
	while (ep != NULL) {
		printf("  %d", ep->time_of_event);
		ep = (EVENT *) ep->queue;
	}
	printf("  NULL\n");
	ReleaseLock(EventLock, "PrintEventQueue");
	return;
}             // End of PrintEventQueue

/*****************************************************************

 DequeueItemFromEventQueue()

 Deque a specified item from the event queue.
 Actions include:
 o Start at the head of the queue.
 o Hunt along until we find a matching identifier.
 o Dequeue it.
 o Return the pointer to the structure to the caller.

 error not 0 means the event wasn't found;
 *****************************************************************/

void DequeueItemFromEventQueue(EVENT *event_ptr, INT32 *error) {
	EVENT *last_ptr;
	EVENT *temp_ptr;

	// It's possible that HardwareTimer will call us when it
	// thinks there's a timer event, but in fact the
	// HardwareInterrupt has handled the timer event.
	// in that case, the event_ptr may be null.
	// If so, eat the error.
	//if ( event_ptr == NULL )   {  // It's already Dequeued
	//    return;
	//}
	if (event_ptr->structure_id != EVENT_STRUCTURE_ID) {
		printf("Bad structure id read in DequeueItem.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}

	GetLock(EventLock, "DequeueItem");
	*error = 0;
	temp_ptr = (EVENT *) EventQueue.queue;
	last_ptr = &EventQueue;
	while (1) {
		if (temp_ptr == NULL) {
			*error = 1;
			break;
		}
		if (temp_ptr == event_ptr) {
			last_ptr->queue = temp_ptr->queue;
			event_ptr->queue = (INT32 *) NULL;
			break;
		}
		last_ptr = temp_ptr;
		temp_ptr = (EVENT *) temp_ptr->queue;
	} /* End while                */
	if (ReleaseLock(EventLock, "DequeueItem") == FALSE)
		printf("Took error on ReleaseLock in DequeueItem\n");

}                     // End   DequeueItemFromEventQueue

/*****************************************************************

 GetNextEventTime()

 Look in the event queue.  Don't dequeue anything,
 but just read the time of the first event.

 return a -1 if there's nothing on the queue
 - the caller must check for this.
 *****************************************************************/

void GetNextEventTime(INT32 *time_of_next_event) {
	EVENT *ep;

	GetLock(EventLock, "GetNextEventTime");
	*time_of_next_event = -1;
	if (EventQueue.queue == NULL) {
		if (ReleaseLock(EventLock, "GetNextEventTime") == FALSE)
			printf("Took error on ReleaseLock in GetNextEventTime\n");
		return;
	}
	ep = (EVENT *) EventQueue.queue;
	if (ep->structure_id != EVENT_STRUCTURE_ID) {
		printf("Bad structure id read in GetNextEventTime.\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	*time_of_next_event = ep->time_of_event;
	if (ReleaseLock(EventLock, "GetNextEventTime") == FALSE)
		printf("Took error on ReleaseLock in GetNextEventTime\n");

}                   // End of GetNextEventTime

/*****************************************************************

 PrintHardwareStats()

 This routine is called when the simulation halts.  It prints out
 the various usages of the hardware that have occurred during the
 simulation.
 *****************************************************************/

void PrintHardwareStats(void) {
	INT32 i, temp;
	double MeanNumberRunningProcesses = 0;
	double util; /* This is in range 0 - 1       */

	printf("\nHardware Statistics during the Simulation\n");
	for (i = 0; i < MAX_NUMBER_OF_DISKS ; i++) {
		temp = HardwareStats.disk_reads[i] + HardwareStats.disk_writes[i];
		if (temp > 0) {
			printf("Disk %2d: Disk Reads = %5d: Disk Writes = %5d: ", i,
					HardwareStats.disk_reads[i], HardwareStats.disk_writes[i]);
			util = (double) HardwareStats.time_disk_busy[i]
					/ (double) CurrentSimulationTime;
			printf("Disk Utilization = %6.3f\n", util);
		}
	}
	if (HardwareStats.number_faults > 0)
		printf("Faults = %5d:  ", HardwareStats.number_faults);

	MeanNumberRunningProcesses =
			(double) HardwareStats.TimeWeightedNumberRunningProcesses
					/ (double) HardwareStats.context_switches;
	if ((double) HardwareStats.TimeWeightedNumberRunningProcesses == 0)
		MeanNumberRunningProcesses = 1.0;

	if (HardwareStats.context_switches > 0)
		printf(
				"Context Switches = %5d:  Mean Number Running Threads = %6.2f   ",
				HardwareStats.context_switches, MeanNumberRunningProcesses);
	printf("CALLS = %5d\n  ", HardwareStats.number_charge_times);

}               // End of PrintHardwareStats
/*****************************************************************

 PrintRingBuffer()

 This is called by the panic code to print out what's
 been happening with events and interrupts.
 *****************************************************************/

void PrintRingBuffer(void) {
	INT16 index;
	INT16 next_print;

	if (EventRingBuffer[0].time_of_request == 0)
		return; /* Never used - ignore       */
	GetLock(EventLock, "PrintRingBuffer");
	next_print = EventRingBuffer_index;

	printf("Current time is %d\n\n", CurrentSimulationTime);
	printf("Record of Hardware Requests:\n\n");
	printf("This is a history of the last events that were requested.  It\n");
	printf("is a ring buffer so note that the times are not in order.\n\n");
	printf("Column A: Time at which the OS made a request of the hardware.\n");
	printf("Column B: Time at which the hardware was expected to give an\n");
	printf("          interrupt.\n");
	printf("Column C: The actual time at which the hardware caused an\n");
	printf("          interrupt.  This should be the same or later than \n");
	printf("          Column B.  If this number is -1, then the event \n");
	printf("          occurred because the request was superceded by a \n");
	printf("          later event.\n");
	printf("Column D: Device Type.  4 = Timer, 5... are disks \n");
	printf("Column E: Device Status.  0 indicates no error.  You should\n");
	printf("          worry about anything that's not 0.\n\n");
	printf("Column A    Column B      Column C      Column D   Column E\n\n");

	for (index = 0; index < EVENT_RING_BUFFER_SIZE; index++) {
		next_print++;
		next_print = next_print % EVENT_RING_BUFFER_SIZE;

		printf("%7d    %7d    %8d     %8d    %8d\n",
				EventRingBuffer[next_print].time_of_request,
				EventRingBuffer[next_print].expected_time_of_event,
				EventRingBuffer[next_print].real_time_of_event,
				EventRingBuffer[next_print].event_type,
				EventRingBuffer[next_print].event_error);
	}
	if (ReleaseLock(EventLock, "PrintRingBuffer") == FALSE)
		printf("Took error on ReleaseLock in PrintRingBuffer\n");

}                    // End of PrintRingBuffer

/*****************************************************************

 GetSectorStructure()

 Determine if the requested sector exists, and if so hand back the
 location in memory where we've stashed data for this sector.

 Actions include:
 o Hunt along the sector data until we find the appropriate sector.
 o Return the address of the sector data.

 Error not 0 means the structure wasn't found.  This means that
 no one has used this particular sector before and thus it hasn't
 been written to.
 *****************************************************************/

void GetSectorStructure(INT16 disk_id, INT16 sector, char **sector_ptr,
		INT32 *error) {
	SECTOR *temp_ptr;

	*error = 0;
	temp_ptr = (SECTOR *) sector_queue[disk_id].queue;
	while (1) {
		if (temp_ptr == NULL) {
			*error = 1;
			break;
		}

		if (temp_ptr->structure_id != SECTOR_STRUCTURE_ID) {
			printf("Bad structure id read in GetSectorStructure.\n");
			HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
		}

		if (temp_ptr->sector == sector) {
			*sector_ptr = (temp_ptr->sector_data);
			break;
		}
		temp_ptr = (SECTOR *) temp_ptr->queue;
	}        // End while
}                // End GetSectorStructure

/*****************************************************************

 CreateSectorStruct()

 This is the routine that will create a sector structure and add it
 to the list of valid sectors.

 Actions include:
 o Allocate a structure for the event.
 o Fill in the structure.
 o Enqueue it.
 o Pass back the pointer to the sector data.

 WARNING: NO CHECK is made to ensure a structure for this sector
 doesn't exist.  The assumption is that the caller has previously
 failed a call to GetSectorStructure.
 *****************************************************************/

void CreateSectorStruct(INT16 disk_id, INT16 sector, char **returned_sector_ptr) {
	SECTOR *ssp;

	ssp = (SECTOR *) malloc(sizeof(SECTOR));
	if (ssp == NULL) {
		printf("We didn't complete the malloc in CreateSectorStruct.\n");
		printf("A malloc returned with a NULL pointer.\n");
		HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
	}
	ssp->structure_id = SECTOR_STRUCTURE_ID;
	ssp->disk_id = disk_id;
	ssp->sector = sector;
	*returned_sector_ptr = (ssp->sector_data);

	/* Enqueue this new structure on the HEAD of the queue for this disk   */
	ssp->queue = sector_queue[disk_id].queue;
	sector_queue[disk_id].queue = (INT32 *) ssp;

}                                    // End of CreateSectorStruct

/**************************************************************************
 **************************************************************************
 THREAD MANAGER
 What follows is a series of routines that manage the threads and
 synchronization for both Windows and LINUX.

 PrintThreadTable - Used for debugging - prints out all the info for
 the thread table that contains all thread info.
 Z502CreateUserThread - Called only by test.c to establish the initial
 threads that will later have the ability to act as processes.

 +++ These routines associate a thread with a context and run it +++
 Z502PrepareProcessForExecution - A thread has been created.  Here we give
 it the ability to be suspended by assigning it a Condition
 and a Mutex.  We then suspend it until it's ready to be
 scheduled.
 AssociateContextWithProcess - Called by Z502MakeContext - it takes the thread
 to the next stage of preparation by giving it a context.
 Meanwhile, the thread continues to be Suspended.

 ResumeProcessExecution - A thread tells the target thread to wake up.  At
 this point in Rev 4.0, that thread will then suspend itself.
 SuspendProcessExecution - Suspend ourself.  A thread that wants to resume
 us can do so because it can find our Context, our Condition,
 and our Mutex.
 CreateAThread - Called by Z502CreateUserThread and also by the Z502 in
 order to create the thread used as the interrupt thread.
 DestroyThread - There's code here to destroy a thread, but in Rev 4.0
 noone is calling it and its success is unknown.
 ChangeThreadPriority - Used by Z502Init to change the priority of the user
 thread.  Perhaps ALL user threads should have their
 priority adjusted!
 GetMyTid -     Returns the thread ID of the calling thread.
 given multiple threads in Rev 4.0.

 **************************************************************************
 **************************************************************************/

/**************************************************************************
 PrintThreadTable
 Used for debugging.  Prints out the state of the user threads - where
 they are in the creation process.  Once they get going, we assume
 that the threads will take care of themselves.
 have the ability to act as processes.

 **************************************************************************/
void PrintThreadTable(char *Explanation) {
	if (DEBUG_USER_THREADS) {
		int i = 0;
		printf("\n\n");
		printf("%s", Explanation);
		for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
			if (ThreadTable[i].CurrentState > 2) {
				printf(
						"LocalID: %d  ThreadID:  %d   CurrentState:  %d   Context:  %lx  Condition: %d   Mutex  %d\n",
						ThreadTable[i].OurLocalID, ThreadTable[i].ThreadID,
						ThreadTable[i].CurrentState,
						(unsigned long) ThreadTable[i].Context,
						ThreadTable[i].Condition, ThreadTable[i].Mutex);
			}
		}
	}  //End DEBUG
}                           // End of PrintThreadTable

/**************************************************************************
 Z502CreateUserThread
 Called only by test.c to establish the initial threads that will later
 have the ability to act as processes.
 We set up a table that associates our local thread number, the ThreadID
 of the thread, and later when we have a CONTEXT, we associate that
 as well.
 The thread that's created goes off to whereever it was supposed to start.
 **************************************************************************/

void Z502CreateUserThread(void *ThreadStartAddress) {
	int ourLocalID = -1;
	int i;
	// If this is our first time in the hardware, do some initializations
	if (Z502Initialized == FALSE)
		Z502Init();
	GetLock(ThreadTableLock, "Z502CreateUserThread");
	// Find out the first uninitialized thread
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].OurLocalID == -1) {
			ourLocalID = i;
			break;
		}
	}
	// We are responsible for the call of this routine in main of test.c,
	// so if we run out of structures, it's our own fault.  So call this a
	// hardware error.
	if (ourLocalID == -1) {
		printf("Error 1 in Z502CreateUserThread\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}

	ThreadTable[ourLocalID].OurLocalID = ourLocalID;
	ThreadTable[ourLocalID].ThreadID = CreateAThread(ThreadStartAddress,
			&ourLocalID);
	ThreadTable[ourLocalID].Context = (Z502CONTEXT *) -1;
	ThreadTable[ourLocalID].CurrentState = CREATED;
	PrintThreadTable("Z502CreateUserThread\n");
	ReleaseLock(ThreadTableLock, "Z502CreateUserThread");
}                          // End of Z502CreateUserThread

/**************************************************************************
 Z502PrepareProcessForExecution()
 A new thread is generated by Z502CreateUserThread - called by main in
 test.c.  That starts up the thread, again at a routine in test.c.
 That new thread immediately calls in here.
 We perform the following actions here:

 1.  Check that we indeed know about this thread - bogus actions do occur.
 2.  Set the CurrentState as SUSPENDED_WAITING_FOR_CONTEXT.
 3.  Do a CreateLock
 4.  Do a CreateCondition
 5.  Suspend the thread by doing a WaitForCondition()
 6.  Unknown to this code, the Z502MakeContext code will call
 AssociateContextWithProcess() which will look in the
 ThreadTable and find a thread with state SUSPENDED_WAITING_FOR_CONTEXT.
 7.  Z502MakeContext will fill in the context in ThreadTable and set a
 thread's CurrentState to SUSPENDED_WAITING_FOR_FIRST_SCHED.
 8.  SwitchContext will match the context it has to the context matched
 with a particular thread.  That thread should have
 SUSPENDED_WAITING_FOR_FIRST_SCHED if it came through this code.
 9.  SwitchContext sets CurrentState to ACTIVE.
 10. SwitchContext does a SignalCondition on this thread.
 11. That means the thread continues in THIS routine.
 12. The thread looks in its Context, finds the address where it is
 to execute, and returns that address to the caller in test.c
 **************************************************************************/
void *Z502PrepareProcessForExecution() {
	int myTid = GetMyTid();
	int ourLocalID = -1;
	int i;
	UINT32 RequestedCondition;
	INT32 RequestedMutex;

	srand(myTid);        // Every thread has a unique random number - 2014
	GetLock(ThreadTableLock, "Z502PrepareProcessForExecution");
	PrintThreadTable("Entering -> PrepareProcessForExecution\n");
	// Find my TID in the table & make sure all is OK
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].ThreadID == myTid) {
			ourLocalID = i;
			break;
		}
	}
	if (ourLocalID == -1) {
		printf("Error 2 in Z502PrepareProcessForExecution\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}

	// Set our state here
	ThreadTable[ourLocalID].CurrentState = SUSPENDED_WAITING_FOR_CONTEXT;

	// And get a condition that we'll wait on and a lock
	CreateCondition(&RequestedCondition);
	ThreadTable[ourLocalID].Condition = RequestedCondition;
	CreateLock(&RequestedMutex, "Z502PrepareProcessForExecution");
	ThreadTable[ourLocalID].Mutex = RequestedMutex;
	ReleaseLock(ThreadTableLock, "Z502PrepareProcessForExecution");
	// Suspend ourselves and don't wake up until we're ready to do real work
	while (ThreadTable[ourLocalID].CurrentState == SUSPENDED_WAITING_FOR_CONTEXT) {
		//ReleaseLock( ThreadTableLock, "Z502PrepareProcessForExecution" );
		WaitForCondition(ThreadTable[ourLocalID].Condition,
				ThreadTable[ourLocalID].Mutex, 30,
				"Z502PrepareProcessForExecution");
	}
	// Now "magically", when we are awakened, we have a Context associated
	// with us and our state should be  ACTIVE
	GetLock(ThreadTableLock, "Z502PrepareProcessForExecution");
	PrintThreadTable("Exiting -> PrepareProcessForExecution\n");
	if (ThreadTable[ourLocalID].Context == (Z502CONTEXT *) -1) {
		printf("Error 3 in Z502PrepareProcessForExecution\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	ReleaseLock(ThreadTableLock, "Z502PrepareProcessForExecution");
	return (void *) ThreadTable[ourLocalID].Context->CodeEntry;
}                                       // End of Z502PrepareProcessForExecution

/**************************************************************************
 AssociateContextWithProcess

 Find a thread that's waiting for a Context and give it one.
 **************************************************************************/
void AssociateContextWithProcess(Z502CONTEXT *Context) {
	int ourLocalID = -1;
	int i;
	//GetLock( ThreadTableLock, "AssociateContextWithProcess" );
	PrintThreadTable("Entering -> AssociateContextWithProcess\n");
	// Find a thread that needs a context
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].CurrentState == SUSPENDED_WAITING_FOR_CONTEXT) {
			ourLocalID = i;
			break;
		}
	}
	if (ourLocalID == -1) {
		printf("Error 4 in AssociateContextWithProcess()\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	ThreadTable[ourLocalID].Context = Context;
	ThreadTable[ourLocalID].CurrentState = SUSPENDED_WAITING_FOR_FIRST_SCHED;
	PrintThreadTable("Exiting -> AssociateContextWithProcess\n");
	//ReleaseLock( ThreadTableLock, "AssociateContextWithProcess" );
}                          // End of AssociateContextWithProcess

/**************************************************************************
 ResumeProcessExecution

 This wakes up a target thread
 **************************************************************************/
void ResumeProcessExecution(Z502CONTEXT *Context) {
	int ourLocalID = -1;
	int i;
	// Find target Context in the table & make sure all is OK
	GetLock(ThreadTableLock, "ResumeProcessExecution");
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].Context == Context) {
			ourLocalID = i;
			break;
		}
	}
	if (ourLocalID == -1) {
		printf("Error in ResumeProcessExecuton\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	ThreadTable[ourLocalID].CurrentState = ACTIVE;
	PrintThreadTable("ResumeProcessExecution\n");
	SignalCondition(ThreadTable[ourLocalID].Condition,
			"ResumeProcessExecution");
	ReleaseLock(ThreadTableLock, "ResumeProcessExecution");
}                               // End of ResumeProcessExecution

/**************************************************************************
 SuspendProcessExecution

 This suspends a thread - most likely ourselves - don't know about that
 **************************************************************************/
void SuspendProcessExecution(Z502CONTEXT *Context) {
	int ourLocalID = -1;
	int i;
	UINT32 RequestedCondition;
	INT32 RequestedMutex;
	// If the context we're handed here is NULL (0), then it's the
	// original thread that we were running on when the program started.
	// Get a condition and a lock, and suspend ourselves forever
	//GetLock( ThreadTableLock, "SuspendProcessExecution" );
	if (Context == NULL) {
		// And get a condition that we'll wait on and a lock
		CreateCondition(&RequestedCondition);
		//ThreadTable[ourLocalID].Condition = RequestedCondition;
		CreateLock(&RequestedMutex, "SuspendProcessExecution");
		//ThreadTable[ourLocalID].Mutex = RequestedMutex;
		// Note that the wait time is not used by this callee
		WaitForCondition(RequestedCondition, RequestedMutex, -1,
				"SuspendProcessExecution");
		printf("SERIOUS ERROR:  The initial thread has become unsuspended\n");
		return;
	}
	// Find target Context in the table & make sure all is OK
	for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
		if (ThreadTable[i].Context == Context) {
			ourLocalID = i;
			break;
		}
	}
	if (ourLocalID == -1) {
		printf("Error in SuspendProcessExecution\n");
		HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
	}
	PrintThreadTable("SuspendProcessExecution\n");
	//ReleaseLock( ThreadTableLock, "SuspendProcessExecution" );
	WaitForCondition(ThreadTable[ourLocalID].Condition,
			ThreadTable[ourLocalID].Mutex, 30, "SuspendProcessExecution");
}    //  SuspendProcessExecution
/**************************************************************************
 CreateAThread
 There are Linux and Windows dependencies here.  Set up the threads
 for the two systems.
 We return the Thread Handle to the caller.
 **************************************************************************/

int CreateAThread(void *ThreadStartAddress, INT32 *data) {
#ifdef  NT
	DWORD ThreadID;
	HANDLE ThreadHandle;
	if ((ThreadHandle = CreateThread(NULL, 0,
			(LPTHREAD_START_ROUTINE) ThreadStartAddress, (LPVOID) data,
			(DWORD) 0, &ThreadID)) == NULL) {
		printf("Unable to create thread in CreateAThread\n");
		GoToExit(0);
	}
	return ((int) ThreadID);
#endif

#if defined LINUX || defined MAC
	int ReturnCode;
	// int                  SchedPolicy = SCHED_FIFO;   // not used
	int policy;
	struct sched_param param;
	pthread_t Thread;
	pthread_attr_t Attribute;

	ReturnCode = pthread_attr_init( &Attribute );
	if ( ReturnCode != FALSE )
	printf( "Error in pthread_attr_init in CreateAThread\n" );
	ReturnCode = pthread_attr_setdetachstate( &Attribute, PTHREAD_CREATE_JOINABLE );
	if ( ReturnCode != FALSE )
	printf( "Error in pthread_attr_setdetachstate in CreateAThread\n" );
	ReturnCode = pthread_create( &Thread, &Attribute, ThreadStartAddress, data );
	if ( ReturnCode == EINVAL ) /* Will return 0 if successful */
	printf( "ERROR doing pthread_create - The Thread, attr or sched param is wrong\n");
	if ( ReturnCode == EAGAIN ) /* Will return 0 if successful */
	printf( "ERROR doing pthread_create - Resources not available\n");
	if ( ReturnCode == EPERM ) /* Will return 0 if successful */
	printf( "ERROR doing pthread_create - No privileges to do this sched type & prior.\n");

	ReturnCode = pthread_attr_destroy( &Attribute );
	if ( ReturnCode ) /* Will return 0 if successful */
	printf( "Error in pthread_mutexattr_destroy in CreateAThread\n" );
	ReturnCode = pthread_getschedparam( Thread, &policy, &param);
	return( (int)Thread );
#endif
}                                // End of CreateAThread
/**************************************************************************
 DestroyThread
 **************************************************************************/

void DestroyThread(INT32 ExitCode) {
#ifdef   NT
	ExitThread((DWORD) ExitCode);
#endif

#if defined LINUX || defined MAC
//    struct  thr_rtn  msg;
//    strcpy( msg.out_msg, "");
//    msg.completed = ExitCode;
//    pthread_exit( (void *)msg );
#endif
} /* End of DestroyThread   */

/**************************************************************************
 ChangeThreadPriority
 On LINUX, there are two kinds of priority.  The static priority
 requires root privileges so we aren't doing that here.
 The dynamic priority changes based on various scheduling needs -
 we have modist control over the dynamic priority.
 On LINUX, the dynamic priority ranges from -20 (most favorable) to
 +20 (least favorable) so we add and subtract here appropriately.
 Again, in our lab without priviliges, this thread can only make
 itself less favorable.
 For Windows, we will be using two classifications here:
 THREAD_PRIORITY_ABOVE_NORMAL
 Indicates 1 point above normal priority for the priority class.
 THREAD_PRIORITY_BELOW_NORMAL
 Indicates 1 point below normal priority for the priority class.

 **************************************************************************/

void ChangeThreadPriority(INT32 PriorityDirection) {
#ifdef   NT
	INT32 ReturnValue;
	HANDLE MyThreadID;
	MyThreadID = GetCurrentThread();
	if (PriorityDirection == MORE_FAVORABLE_PRIORITY)
		ReturnValue = (INT32) SetThreadPriority(MyThreadID,
		THREAD_PRIORITY_ABOVE_NORMAL);
	if (PriorityDirection == LESS_FAVORABLE_PRIORITY)
		ReturnValue = (INT32) SetThreadPriority(MyThreadID,
		THREAD_PRIORITY_BELOW_NORMAL);
	if (ReturnValue == 0) {
		printf("ERROR:  SetThreadPriority failed in ChangeThreadPriority\n");
		HandleWindowsError();
	}

#endif
#if defined LINUX || defined MAC
	// 09/2011 - I have attempted to make the interrupt thread a higher priority
	// than the base thread but have not been successful.  It's possible to change
	// the "nice" value for the whole process, but not for individual threads.
	//int                  policy;
	//struct sched_param   param;
	//int                  CurrentPriority;
	//CurrentPriority = getpriority( PRIO_PROCESS, 0 );
	//ReturnValue = setpriority( PRIO_PROCESS, 0, CurrentPriority - PriorityDirection );
	//ReturnValue = setpriority( PRIO_PROCESS, 0, 15 );
	//CurrentPriority = getpriority( PRIO_PROCESS, 0 );
	//ReturnValue = pthread_getschedparam( GetMyTid(), &policy, &param);

	//if ( ReturnValue == ESRCH || ReturnValue == EINVAL || ReturnValue == EPERM )
	//    printf( "ERROR in ChangeThreadPriority - Input parameters are wrong\n");
	//if ( ReturnValue == EACCES )
	//    printf( "ERROR in ChangeThreadPriority - Not privileged to do this!!\n");

#endif
}                         // End of ChangeThreadPriority

/**************************************************************************
 GetMyTid
 Returns the current Thread ID
 **************************************************************************/
int GetMyTid() {
#ifdef   NT
	return ((int) GetCurrentThreadId());
#endif
#ifdef   LINUX
	return( (int)pthread_self() );
#endif
#ifdef   MAC
	return( (unsigned long int)pthread_self() );
#endif
}                                   // End of GetMyTid

/**************************************************************************
 **************************************************************************
 LOCKS MANAGER
 What follows is a series of routines that manage the threads and
 synchronization for both Windows and LINUX.

 CreateLock  - Generate a new lock.
 GetTryLock - GetTryLock tries to get a lock.  If it is successful, it
 returns 1.  If it is not successful, and the lock is held by someone
 else, a 0 is returned.
 GetLock     - Can handle locks for either Windows or LINUX.
 Assumes the lock has been established by the caller.
 ReleaseLock - Can handle locks for either Windows or LINUX.
 Assumes the lock has been established by the caller.
 PrintLockDebug - prints entrances and exits to the locks.

 **************************************************************************
 **************************************************************************/
#define     LOCK_ENTER              1
#define     LOCK_EXIT               0

#define     LOCK_CREATE             0
#define     LOCK_TRY                1
#define     LOCK_GET                2
#define     LOCK_RELEASE            3

/**************************************************************************
 CreateLock
 **************************************************************************/
void CreateLock(INT32 *RequestedMutex, char *CallingRoutine) {
	int ErrorFound = FALSE;
#ifdef NT
	HANDLE MemoryMutex;
	// Create with no security, no initial ownership, and no name
	if ((MemoryMutex = CreateMutex(NULL, FALSE, NULL)) == NULL)
		ErrorFound = TRUE;
	*RequestedMutex = (UINT32) MemoryMutex;
#endif
#if defined LINUX || defined MAC

	pthread_mutexattr_t Attribute;

	ErrorFound = pthread_mutexattr_init( &Attribute );
	if ( ErrorFound != FALSE )
	printf( "Error in pthread_mutexattr_init in CreateLock\n" );
#if defined LINUX
	ErrorFound = pthread_mutexattr_settype( &Attribute, PTHREAD_MUTEX_ERRORCHECK_NP );
#endif
#if defined MAC
	ErrorFound = pthread_mutexattr_settype( &Attribute, PTHREAD_MUTEX_ERRORCHECK );
#endif
	if ( ErrorFound != FALSE )
	printf( "Error in pthread_mutexattr_settype in CreateLock\n" );
	ErrorFound = pthread_mutex_init( &(LocalMutex[NextMutexToAllocate]), &Attribute );
	if ( ErrorFound ) /* Will return 0 if successful */
	printf( "Error in pthread_mutex_init in CreateLock\n" );
	ErrorFound = pthread_mutexattr_destroy( &Attribute );
	if ( ErrorFound ) /* Will return 0 if successful */
	printf( "Error in pthread_mutexattr_destroy in CreateLock\n" );
	*RequestedMutex = NextMutexToAllocate;
	NextMutexToAllocate++;
#endif
	if (ErrorFound == TRUE) {
		printf("We were unable to create a mutex in CreateLock\n");
		GoToExit(0);
	}
	PrintLockDebug(LOCK_CREATE, CallingRoutine, *RequestedMutex, LOCK_EXIT);
}                               // End of CreateLock

/**************************************************************************
 GetTryLock
 GetTryLock tries to get a lock.  If it is successful, it returns 1.
 If it is not successful, and the lock is held by someone else,
 a 0 is returned.

 The pthread_mutex_trylock() function tries to lock the specified mutex.
 If the mutex is already locked, an error is returned.
 Otherwise, this operation returns with the mutex in the locked state
 with the calling thread as its owner.
 **************************************************************************/

int GetTryLock(UINT32 RequestedMutex, char *CallingRoutine) {
	int ReturnValue = FALSE;
	int LockReturn;
#ifdef   NT
	HANDLE MemoryMutex;
#endif

	PrintLockDebug(LOCK_TRY, CallingRoutine, RequestedMutex, LOCK_ENTER);
#ifdef   NT
	MemoryMutex = (HANDLE) RequestedMutex;
	LockReturn = (int) WaitForSingleObject(MemoryMutex, 1);
//      printf( "Code Returned in GetTryLock is %d\n", LockReturn );
	if (LockReturn == WAIT_FAILED) {
		printf("Internal error in GetTryLock\n");
		HandleWindowsError();
		GoToExit(0);
	}
	if (LockReturn == WAIT_TIMEOUT)   // Timeout occurred with no lock
		ReturnValue = FALSE;
	if (LockReturn == WAIT_OBJECT_0)   // Lock was obtained
		ReturnValue = TRUE;
#endif
#if defined LINUX || defined MAC
	LockReturn = pthread_mutex_trylock( &(LocalMutex[RequestedMutex]) );
//    printf( "Code Returned in GetTRyLock is %d\n", LockReturn );

	if ( LockReturn == EINVAL )
	printf( "PANIC in GetTryLock - mutex isn't initialized\n");
	if ( LockReturn == EFAULT )
	printf( "PANIC in GetTryLock - illegal address for mutex\n");
	if ( LockReturn == EBUSY )//  Already locked by another thread
	ReturnValue = FALSE;
	if ( LockReturn == EDEADLK )//  Already locked by this thread
	ReturnValue = TRUE;// Here we eat this error
	if ( LockReturn == 0 )//  Not previously locked - all OK
	ReturnValue = TRUE;
#endif
	PrintLockDebug(LOCK_TRY, CallingRoutine, RequestedMutex, LOCK_EXIT);
	return (ReturnValue);
}                    // End of GetTryLock

/**************************************************************************
 GetLock
 This routine should normally only return when the lock has been gotten.
 It should normally return TRUE - and returns FALSE only on an error.
 Note that Windows and Linux operate differently here.  Windows does
 NOT take an error if a lock attempt is made by a thread already holding
 the lock.
 **************************************************************************/

int GetLock(UINT32 RequestedMutex, char *CallingRoutine) {
	INT32 LockReturn;
	int ReturnValue = FALSE;
#ifdef   NT
	HANDLE MemoryMutex = (HANDLE) RequestedMutex;
#endif
	PrintLockDebug(LOCK_GET, CallingRoutine, RequestedMutex, LOCK_ENTER);
#ifdef   NT
	LockReturn = WaitForSingleObject(MemoryMutex, INFINITE);
	if (LockReturn != 0) {
		printf("Internal error waiting for a lock in GetLock\n");
		HandleWindowsError();
		GoToExit(0);
	}
	if (LockReturn == 0)    //  Not previously locked - all OK
		ReturnValue = TRUE;
#endif

#if defined LINUX || defined MAC
//        printf( "GetLock %d\n",  LocalMutex[RequestedMutex].__data.__owner );
//        if ( LocalMutex[RequestedMutex].__data.__owner > 0 )   {
//            printf("GetLock:  %d %d %d\n", RequestedMutex,
//                    (int)LocalMutex[RequestedMutex], GetMyTid() );
//        }
	LockReturn = pthread_mutex_lock( &(LocalMutex[RequestedMutex]) );
	if ( LockReturn == EINVAL )
	printf( "PANIC in GetLock - mutex isn't initialized\n");
	if ( LockReturn == EFAULT )
	printf( "PANIC in GetLock - illegal address for mutex\n");

	// Note that LINUX acts differently from Windows.  We will eat the
	// error here to get compatibility.
	if ( LockReturn == EDEADLK ) {  //  Already locked by this thread
		LockReturn = 0;
		// printf( "ERROR - Already locked by this thread\n");
	}
	if ( LockReturn == 0 )  //  Not previously locked - all OK
	ReturnValue = TRUE;
#endif
	PrintLockDebug(LOCK_GET, CallingRoutine, RequestedMutex, LOCK_EXIT);
	return (ReturnValue);
}                              // End of GetLock

/**************************************************************************
 ReleaseLock
 If the function succeeds, the return value is TRUE.
 If the function fails, and the Mutex is NOT unlocked,
 then FALSE is returned.
 **************************************************************************/
int ReleaseLock(UINT32 RequestedMutex, char* CallingRoutine) {
	int ReturnValue = FALSE;
	int LockReturn;
#ifdef   NT
	HANDLE MemoryMutex = (HANDLE) RequestedMutex;
#endif
	PrintLockDebug(LOCK_RELEASE, CallingRoutine, RequestedMutex, LOCK_ENTER);

#ifdef   NT
	LockReturn = ReleaseMutex(MemoryMutex);

	if (LockReturn != 0)    // Lock was released
		ReturnValue = TRUE;
#endif
#if defined LINUX || defined MAC
	LockReturn = pthread_mutex_unlock( &(LocalMutex[RequestedMutex]) );
//    printf( "Return Code in Release Lock = %d\n", LockReturn );

	if ( LockReturn == EINVAL )
	printf( "PANIC in ReleaseLock - mutex isn't initialized\n");
	if ( LockReturn == EFAULT )
	printf( "PANIC in ReleaseLock - illegal address for mutex\n");
	if (DEBUG_LOCKS) {
		if ( LockReturn == EPERM )    //  Not owned by this thread
		printf( "ERROR - Lock is not currently locked by this thread.  Caller = %s\n",
				CallingRoutine);
	}
	if ( LockReturn == 0 )    //  Successfully unlocked - all OK
	ReturnValue = TRUE;
#endif
	PrintLockDebug(LOCK_RELEASE, CallingRoutine, RequestedMutex, LOCK_EXIT);
	return (ReturnValue);
}            // End of ReleaseLock
/**************************************************************************
 PrintLockDebug
 Print out message indicating what's happening with locks
 This code updated 06/2013 to account for many possible user/base level
 threads
 **************************************************************************/
#define     MAX_NUMBER_OF_LOCKS    64
#define     MAX_NUMBER_OF_LOCKERS  10

typedef struct {
	int NumberOfLocks;
	int LockID[MAX_NUMBER_OF_LOCKS];
	int LockCount[MAX_NUMBER_OF_LOCKS];
	//int LockOwner[MAX_NUMBER_OF_LOCKS];
	int LockOwners[MAX_NUMBER_OF_LOCKS][MAX_NUMBER_OF_LOCKERS];
} LOCKING_DB;

INT32 PLDLock;
LOCKING_DB LockDB;
void Quickie(int LockID, int x) {
	int j;
	for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
		printf("%X  ", LockDB.LockOwners[LockID][j]);
	};
	printf(" %d\n", x);
}
void PrintLockDebug(int Action, char *LockCaller, int Mutex, int Return) {
	if (DEBUG_LOCKS) {
		int MyTid, i, j, LockID;
		char TaskID[120], WhichLock[120];
		char Direction[120];
		char Output[120];
		char ProblemReport[120];           // Reports something not-normal
		char LockAction[120];              // What type of lock is calling us
		static int FirstTime = TRUE;

		// Don't do anything if we're logging ourself
		if (strncmp(LockCaller, "PLD", 3) == 0)
			return;
		// This is the initialization the first time we come in here
		if (FirstTime) {
			FirstTime = FALSE;
			CreateLock(&PLDLock, "PLD");
			for (i = 0; i < MAX_NUMBER_OF_LOCKS; i++) {
				LockDB.LockCount[i] = 0;
				//    LockDB.LockOwner[i] = 0;
				LockDB.LockID[i] = 0;
				for (j = 0; j < MAX_NUMBER_OF_LOCKERS; j++) {
					LockDB.LockOwners[i][j] = 0;
				}
			}
			LockDB.NumberOfLocks = 0;
		}

		GetLock(PLDLock, "PLD");
		// Find the string for the type of lock we're doing.
		if (Action == LOCK_CREATE)
			strcpy(LockAction, "CreLock");
		if (Action == LOCK_TRY)
			strcpy(LockAction, "TryLock");
		if (Action == LOCK_GET)
			strcpy(LockAction, "GetLock");
		if (Action == LOCK_RELEASE)
			strcpy(LockAction, "RelLock");

		// Determine if we are "Base" or "interrupt" - Actually there are many
		// Possible base threads - and we will print out which one.
		MyTid = GetMyTid();
		strcpy(TaskID, "Base ");
		if (MyTid == InterruptTid) {
			strcpy(TaskID, "Int  ");
		}

		// Identify which lock we're working on.  Some of the common ones are named,
		//   But many are associated with individual threads, or with locks created
		//   by students in which case they will be named  "Oth...".

		sprintf(WhichLock, "Oth%d   ", Mutex);
		if (Mutex == EventLock)
			strcpy(WhichLock, "Event  ");
		if (Mutex == InterruptLock)
			strcpy(WhichLock, "Int    ");
		if (Mutex == HardwareLock)
			strcpy(WhichLock, "Hard   ");
		if (Mutex == ThreadTableLock)
			strcpy(WhichLock, "T-Tbl  ");

		// We maintain a record of all locks in the order in which they are first
		//  accessed here.  The order doesn't matter, since we can identify that
		//  lock every time we enter here.
		LockID = -1;
		for (i = 0; i < LockDB.NumberOfLocks; i++) {
			if (LockDB.LockID[i] == Mutex)
				LockID = i;
		}
		if (LockID == -1) {
			LockDB.LockID[LockDB.NumberOfLocks] = Mutex;
			LockDB.NumberOfLocks++;
		}

		strcpy(ProblemReport, "");
		// We are ENTERING a TryLock OR Lock - record everything
		// We have the following situations:
		// Case 1: The Lock is not currently held
		//         GOOD - increment the count and record the thread
		// Case 2: The lock is currently held by the new requester
		//         BAD - because we'll have to release it multiple times
		// Case 3: The lock is currently held by someone else
		//         OK - we assume the current holder will release it -
		//         after all, this is what locks are for.
		if (Return == LOCK_ENTER
				&& (Action == LOCK_TRY || Action == LOCK_GET)) {
			strcpy(Direction, " Enter");
			LockDB.LockCount[LockID]++;
			if (LockDB.LockCount[LockID] > MAX_NUMBER_OF_LOCKERS) {
				LockDB.LockCount[LockID] = MAX_NUMBER_OF_LOCKERS;
				printf("LOCK: BAD:  Exceeding number of lockers\n");
			}

			if (LockDB.LockCount[LockID] == 1) {    // Case 1: First lock
				LockDB.LockOwners[LockID][LockDB.LockCount[LockID] - 1] = MyTid;
			} else {                                   // Already locked
				if (MyTid == LockDB.LockOwners[LockID][0]) { // Case 2: Already owned by me - BAD
					sprintf(ProblemReport,
							"LOCK: BAD#1: Thread %X is RELOCKING %s:  Count = %d\n",
							MyTid, WhichLock, LockDB.LockCount[LockID]);
				}
				if (MyTid != LockDB.LockOwners[LockID][0]) { // Case 3: owned by someone else - OK
					sprintf(ProblemReport,
							"LOCK: OK: Thread %X is LOCKING %s Held by %X:  Count = %d\n",
							MyTid, WhichLock, LockDB.LockOwners[LockID][0],
							LockDB.LockCount[LockID]);
					LockDB.LockOwners[LockID][LockDB.LockCount[LockID] - 1] =
							MyTid;
				}
			}           // Lock count NOT 1
		}               // End of entering LOCK

		// We are ENTERING a ReleaseLock
		// We have the following situations:
		// Case 1: The Lock is  currently held by the releaser
		//         GOOD - decrement the count - if there's someone waiting,
		//         do some bookkeeping
		// Case 1A: The lock count is still > 0 - it's still locked.  Report it.
		// Case 2: The lock is currently held by someone else
		//         BAD - We shouldn't be releasing a lock we don't hold
		// Case 3: The lock is currently not held
		//         BAD - this should NOT happen

		if (Return == LOCK_ENTER && Action == LOCK_RELEASE) {
			strcpy(Direction, " Enter");
			Quickie(LockID, 1);

			// Case 1: A thread is releasing a lock it holds
			if (LockDB.LockCount[LockID] > 0
					&& (MyTid == LockDB.LockOwners[LockID][0])) {
				LockDB.LockCount[LockID]--;

				for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
					LockDB.LockOwners[LockID][j] = LockDB.LockOwners[LockID][j
							+ 1];
				}
				for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
					if (j >= LockDB.LockCount[LockID])
						LockDB.LockOwners[LockID][j] = 0;
				}
				//Quickie(LockID, 2);
				// If I've done multiple locks, there may be nobody else locking
				if (LockDB.LockOwners[LockID][0] == 0)
					LockDB.LockOwners[LockID][0] = MyTid;

				if (LockDB.LockCount[LockID] > 0) {    // Case 1A:
					if (MyTid == LockDB.LockOwners[LockID][0]) {
						//Quickie(LockID, 3);
						sprintf(ProblemReport,
								"LOCK: BAD#2: Thread %X is RELEASING %s  But count is = %d\n",
								MyTid, WhichLock, LockDB.LockCount[LockID]);
					}
				}
			}    // End of Case 1

			// Case 2: Make sure a thread only releases a lock it holds
			else if (LockDB.LockCount[LockID] > 0
					&& (MyTid != LockDB.LockOwners[LockID][0])) {
				Quickie(LockID, 3);
				sprintf(ProblemReport,
						"LOCK: BAD#3: Thread %X is RELEASING %s Held by %X:  Count = %d\n",
						MyTid, WhichLock, LockDB.LockOwners[LockID][0],
						LockDB.LockCount[LockID]);
				//LockDB.LockNextOwner[i] = MyTid;
			}

			// Case 3:  Lock not held but still trying to release
			else if (LockDB.LockCount[LockID] == 0) { // First release - this is good
				sprintf(ProblemReport,
						"LOCK: BAD#4: Thread %X is RELEASING %s:  Count = %d\n",
						MyTid, WhichLock, LockDB.LockCount[LockID]);
			} else {
				sprintf(ProblemReport,
						"LOCK: BAD#5: Thread %X is RELEASING %s:  With a condition we didn't account fo: Count = %d\n",
						MyTid, WhichLock, LockDB.LockCount[LockID]);
			}
		}    // End of entering UNLOCK

		// Leaving the lock or unlock routine
		if (Return == LOCK_EXIT) {
			strcpy(Direction, " Exit ");
			// With multiple requesters, there's no assurance that the release will be in FIFO order
			// Check the exit to assure that the locker leaving the lock, and now the owner is
			// in fact the first one listed in the database
			if ((Action == LOCK_TRY || Action == LOCK_GET)
					&& MyTid != LockDB.LockOwners[LockID][0]) {
				i = 0;
				for (j = 1; j <= LockDB.LockCount[LockID]; j++) {
					if (MyTid == LockDB.LockOwners[LockID][j])
						i = j;
				}
				if (i == 0) {
					sprintf(ProblemReport,
							"LOCK: BAD#6: On exit from a lock, Thread %X couldn't find itself in the lockDB for %s\n",
							MyTid, WhichLock);
				} else { // Switch the order so this Task is the owner in our DB
					j = LockDB.LockOwners[LockID][i];
					LockDB.LockOwners[LockID][i] = LockDB.LockOwners[LockID][0];
					LockDB.LockOwners[LockID][0] = j;
					Quickie(LockID, 6);
				}
			}             // It's a lock action and we're not the lock owner
		}                 // END of Return == EXIT
		sprintf(Output, "LOCKS: %s: %s %s  %s Time = %d TID = %X  %s\n",
				LockAction, Direction, TaskID, WhichLock, CurrentSimulationTime,
				MyTid, LockCaller);
		printf("%s", Output);

// If something has gone wrong, print out a report.
		if (strlen(ProblemReport) > 0)
			printf("%s", ProblemReport);
		ReleaseLock(PLDLock, "PLD");
	}  // End  if  DEBUG_LOCKS
}                                 // End of PrintLockDebug

/**************************************************************************
 Condition Management
 Conditions are use for signals - the mechanism for putting threads to
 sleep and waking them up.  On Windows these are called events, and the
 routines here are OS dependent.

 CreateCondition - Get an Event/Condition and store it away.
 WaitForCondition - We're handed the condition for our thread and told to
 wait until some other thread wakes us up.
 SignalCondition - wake up some other thread based on the condition we have.
 **************************************************************************/
/**************************************************************************
 CreateCondition
 **************************************************************************/
void CreateCondition(UINT32 *RequestedCondition) {
	int ConditionReturn;
#ifdef NT
	LocalEvent[NextConditionToAllocate] = CreateEvent(NULL, // no security attributes
			FALSE,     // auto-reset event
			FALSE,     // initial state is NOT signaled
			NULL);     // object not named
	ConditionReturn = 0;
	if (LocalEvent[NextConditionToAllocate] == NULL) {
		printf("Internal error Creating an Event in CreateCondition\n");
		HandleWindowsError();
		GoToExit(0);
	}
#endif

#if defined LINUX || defined MAC
	*RequestedCondition = -1;
	ConditionReturn
	= pthread_cond_init( &(LocalCondition[NextConditionToAllocate]), NULL );

	if ( ConditionReturn == EAGAIN || ConditionReturn == ENOMEM )
	printf( "PANIC in CreateCondition - No System Resources\n");
	if ( ConditionReturn == EINVAL || ConditionReturn == EFAULT)
	printf( "PANIC in CreateCondition - illegal input\n");
	if ( ConditionReturn == EBUSY ) //  Already locked by another thread
	printf( "PANIC in CreateCondition - Already initialized\n");
#endif
	if (ConditionReturn == 0) {
		*RequestedCondition = NextConditionToAllocate;
		NextConditionToAllocate++;
	}
	if (DEBUG_CONDITION) {
		printf("CreateCondition # %d\n", *RequestedCondition);
	}
}                               // End of CreateCondition

/**************************************************************************
 WaitForCondition
 It is assumed that the caller enters here with the mutex locked.
 The result of this call is that the mutex is unlocked.  The caller
 doesn't return from this call until the condition is signaled.
 **************************************************************************/
int WaitForCondition(UINT32 Condition, UINT32 Mutex, INT32 WaitTime,
		char* CallingRoutine) {
	int ReturnValue = 0;
	int ConditionReturn;
	if (DEBUG_CONDITION) {
		printf(
				"WaitForCondition - Enter - time = %d My-Cond = %d  Thread = %X  %s\n",
				CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
	}   // End of DEBUG_CONDITION
#ifdef NT
	ConditionReturn = (int) WaitForSingleObject(LocalEvent[Condition],
	INFINITE);

	if (ConditionReturn == WAIT_FAILED) {
		printf("Internal error waiting for an event in WaitForCondition\n");
		HandleWindowsError();
		GoToExit(0);
	}
	ReturnValue = 0;

#endif
#if defined LINUX || defined MAC
//        if ( LocalMutex[Mutex].__data.__owner == 0 )   {
//            printf("WaitForCondition:  %d %d %d\n", Mutex,
//                    (int)LocalMutex[Mutex], GetMyTid() );
//        }
	pthread_mutex_lock( &(LocalMutex[Mutex]) );
	ConditionReturn
	= pthread_cond_wait( &(LocalCondition[Condition]),
			&(LocalMutex[Mutex]) );
	pthread_mutex_unlock( &(LocalMutex[Mutex]) );
	if ( ConditionReturn == EINVAL )
	printf( "In WaitForCondition, An illegal argument value was found\n");
	if ( ConditionReturn == EPERM )
	printf( "In WaitForCondition, The mutex was not locked by the caller\n");
	if ( ConditionReturn == 0 )
	ReturnValue = TRUE;// Success
#endif
	if (DEBUG_CONDITION) {
		printf(
				"WaitForCondition - Exit - time = %d My-Cond = %d  Thread = %X  %s\n",
				CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
	}   // End of DEBUG_CONDITION

	return (ReturnValue);
}                               // End of WaitForCondition

/**************************************************************************
 SignalCondition
 Used to wake up a thread that's waiting on a condition.
 **************************************************************************/
int SignalCondition(UINT32 Condition, char* CallingRoutine) {
	int ReturnValue = 0;
#ifdef NT
	static int NumberOfSignals = 0;
#endif
#if defined LINUX || defined MAC
	int ConditionReturn;
#endif

	if (DEBUG_CONDITION) {
		printf(
				"SignalCondition - Enter - time = %d Target-Cond = %d  Thread = %X  %s\n",
				CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
	}  // End of DEBUG_CONDITION

	if (InterruptTid == GetMyTid())  // We don't want to signal ourselves
			{
		ReturnValue = TRUE;
		return (ReturnValue);
	}
#ifdef NT
	if (!SetEvent(LocalEvent[Condition])) {
		printf("Internal error signalling  an event in SignalCondition\n");
		HandleWindowsError();
		GoToExit(0);
	}
	ReturnValue = TRUE;
	if (NumberOfSignals % 3 == 0)
		DoSleep(1);
	NumberOfSignals++;
#endif
#if defined LINUX || defined MAC

	ConditionReturn
	= pthread_cond_signal( &(LocalCondition[Condition]) );
	if ( ConditionReturn == EINVAL || ConditionReturn == EFAULT )
	printf( "In SignalCondition, An illegal value or status was found\n");
	if ( ConditionReturn == 0 )
	ReturnValue = TRUE;          // Success
	ConditionReturn = sched_yield();
#endif

	if (DEBUG_CONDITION) {
		printf(
				"SignalCondition - Exit - time = %d Target-Cond = %d  Thread = %X  %s\n",
				CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
	}   // End of DEBUG_CONDITION

	return (ReturnValue);
}                               // End of SignalCondition

/**************************************************************************
 DoSleep
 The argument is the sleep time in milliseconds.  This code should NOT
 be used by the Operating System but is intended only for the hardware.
 **************************************************************************/
void DoSleep(INT32 millisecs) {

#ifdef NT
	Sleep(millisecs);
#endif
#ifndef NT
	usleep((unsigned long) (millisecs * 1000));
#endif
}                              // End of DoSleep

/**************************************************************************
 HandleWindowsError
 **************************************************************************/

#ifdef  NT
void HandleWindowsError() {
	LPVOID lpMsgBuf;
	char OutputString[256];

	FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
					| FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0,
			NULL);
	sprintf(OutputString, "%s\n", (char *) lpMsgBuf);
	printf(OutputString);
}                                     // End HandleWindowsError
#endif

/**************************************************************************
 GoToExit
 This is a good place to put a breakpoint.  It helps you find
 places where the code is diving.
 **************************************************************************/
void GoToExit(int Value) {
	printf("Exiting the program\n");
	exit(Value);
}             // End of GoToExit

/*****************************************************************
 Z502Init()

 We come here in order to initialize the hardware.
 The first call that the OS does to the hardware we want to
 come here.  There are several locations that will get us here.
 *****************************************************************/

void Z502Init() {
	INT16 i;

	if (Z502Initialized == FALSE) {
		// Show that we've been in this code.
		Z502Initialized = TRUE;

		printf("This is Simulation Version %s and Hardware Version %s.\n\n",
		CURRENT_REL, HARDWARE_VERSION);
		EventQueue.queue = NULL;
		CreateLock(&EventLock, "Z502Init");
		CreateLock(&InterruptLock, "Z502Init");
		CreateLock(&HardwareLock, "Z502Init");
		CreateLock(&ThreadTableLock, "Z502Init");
		CreateCondition(&InterruptCondition);
		for (i = 0; i < MAX_NUMBER_OF_DISKS ; i++) {
			sector_queue[i].queue = NULL;
			DiskState[i].LastSector = 0;
			DiskState[i].DiskInUse = FALSE;
			DiskState[i].EventPtr = NULL;
			HardwareStats.disk_reads[i] = 0;
			HardwareStats.disk_writes[i] = 0;
			HardwareStats.time_disk_busy[i] = 0;
		}
		HardwareStats.context_switches = 0;
		HardwareStats.NumberRunningProcesses = 0;
		HardwareStats.TimeWeightedNumberRunningProcesses = 0;
		HardwareStats.number_charge_times = 0;
		HardwareStats.number_faults = 0;

		for (i = 0; i <= LARGEST_STAT_VECTOR_INDEX; i++) {
			STAT_VECTOR[SV_ACTIVE ][i] = 0;
			STAT_VECTOR[SV_VALUE ][i] = 0;
		}

		// These vectors determine where code goes in the OS when an
		// Interrupt, Fault, or SoftwareTrap occurs
		TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR ] = (void *) InterruptHandler;
		TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR ] = (void *) FaultHandler;
		TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR ] = (void *) svc;

		for (i = 0; i < MEMORY_INTERLOCK_SIZE; i++)
			InterlockRecord[i] = -1;

		for (i = 0; i < sizeof(MEMORY); i++)
			MEMORY[i] = i % 256;

		timer_state.timer_in_use = FALSE;
		timer_state.event_ptr = NULL;

		// Set  up the user thread structure - note that it will include
		// both the thread we're running on right now, and also the
		// interrupt thread - since we don't know when someone might
		// access that thread.
		for (i = 0; i < MAX_THREAD_TABLE_SIZE; i++) {
			ThreadTable[i].OurLocalID = -1;
		}

		// Set up the thread table to include ourself, the first thread
		// in the program.  We need it to set the mode, etc.  We will also
		// use the fact that this is number 0 in the Thread Table so that
		// when running in multiprocessor mode, we will indeed suspend
		// this thread.
		ThreadTable[0].OurLocalID = 0;
		ThreadTable[0].ThreadID = GetMyTid();
		ThreadTable[0].Context = (Z502CONTEXT *) NULL;
		ThreadTable[0].CurrentState = CREATED;
		SetMode(KERNEL_MODE);
		SetCurrentContext(NULL);

		ThreadTable[1].OurLocalID = 1;
		ThreadTable[1].ThreadID = CreateAThread((int *) HardwareInterrupt,
				&EventLock);
		ThreadTable[1].Context = (Z502CONTEXT *) NULL;
		ThreadTable[1].CurrentState = CREATED;

		DoSleep(100);
		ChangeThreadPriority(LESS_FAVORABLE_PRIORITY);

	}             // End of Z502Initialized = FALSE
}                     // End of Z502Init
