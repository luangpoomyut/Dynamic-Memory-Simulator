#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "mem.h"

/*
 * This structure serves as the header for each allocated and free block
 * It also serves as the footer for each free block
 * The blocks are ordered in the increasing order of addresses 
 */
typedef struct blk_hdr {                         
        int size_status;
  
    /*
    * Size of the block is always a multiple of 8
    * => last two bits are always zero - can be used to store other information
    *
    * LSB -> Least Significant Bit (Last Bit)
    * SLB -> Second Last Bit 
    * LSB = 0 => free block
    * LSB = 1 => allocated/busy block
    * SLB = 0 => previous block is free
    * SLB = 1 => previous block is allocated/busy
    * 
    * When used as the footer the last two bits should be zero
    */

    /*
    * Examples:
    * 
    * For a busy block with a payload of 20 bytes (i.e. 20 bytes data + an 
    * additional 4 bytes for header)
    * Header:
    * If the previous block is allocated, size_status should be set to 27
    * If the previous block is free, size_status should be set to 25
    * 
    * For a free block of size 24 bytes (including 4 bytes for header + 
    * 4 bytes for footer)
    *
    * Header:
    * If the previous block is allocated, size_status should be set to 26
    * If the previous block is free, size_status should be set to 24
    *
    * Footer:
    * size_status should be 24
    * 
    */
} blk_hdr;

/* Global variable - This will always point to the first block
 * i.e. the block with the lowest address */
blk_hdr *first_blk = NULL;

/*
 * Note: 
 *  The end of the available memory can be determined using end_mark
 *  The size_status of end_mark has a value of 1
 *
 */

/* 
 * Function for allocating 'size' bytes
 * Returns address of allocated block on success 
 * Returns NULL on failure 
 * Here is what this function should accomplish 
 * - Check for sanity of size - Return NULL when appropriate 
 * - Round up size to a multiple of 8 
 * - Traverse the list of blocks and allocate the best free block which can
 *   accommodate the requested size 
 * - Also, when allocating a block - split it into two blocks
 * Tips: Be careful with pointer arithmetic 
 */                    
void* Alloc_Mem(int size) {                          
    // Pointer to keep track of current block being inspected
    blk_hdr* ptr = first_blk;

    // Pointer to keep track of best free block found
    // Initially set to the first_blk
    blk_hdr* best = first_blk;

    // Separate variable to track actual block size needed
    int blockSizeRequired = size;
    
    // Allocated block needs to factor in header size
    blockSizeRequired += 4;

    // Round up blockSizeRequired to the nearest multiple of 8
    if ((blockSizeRequired % 8) != 0) {
        blockSizeRequired = blockSizeRequired + (8 - (blockSizeRequired % 8));
    }

    // Represents the difference in size between request needs and availability
    // Initialized with a sentinel value that is unlikely to be broken
    int deltaSize = 999999;

    // Flag for if perfect fit is found, 1 = found, 0 = not found
    int perfectFit = 0;

    // Flag for allocation being bigger than any block in the heap for the
    // allocation, 1 = too big, 0 = no problem
    int bigEnoughBlock = 0;

    // Flag for an open block existing that can fit the allocation
    // 1 = block found, 0 = block not found
    int openBlockExists = 0;

    // Size of current block (discounting p-bit and a-bit)
    int currBlockSize;

    // Size of current block size_status field          
    int currBlockBitSum;

    // Flag for if p-bit is 1 or not
    // 1 = p-bit is 1, 0 = p-bit is not equal to 1
    // -1 is sentinel initalized value
    int currBlockPBitFlag = -1;

    // Traverse block to block until end of heap
    while ((ptr->size_status) > 1) {        
        currBlockSize = (ptr->size_status) - ((ptr->size_status) % 8);

        currBlockBitSum = (ptr->size_status) % 8;

        // Save p-bit of current block, if currBlockStatus = 2,3,
        // means p-bit is 1
        if (currBlockBitSum == 2 || currBlockBitSum == 3) {
            currBlockPBitFlag = 1;
        } else {
            currBlockPBitFlag = 0;
        }

        // This if statement checks that the size that needs to be allocated is
        // smaller than the current block size
        if (blockSizeRequired <= currBlockSize) {            
            bigEnoughBlock = 1;
            
            // If currBlockStatus has value of 1 or 3 (odd), then we know
            // the a-bit is 1
            if (currBlockBitSum == 0 || currBlockBitSum == 2) {                
                openBlockExists = 1;

                // Difference between block size and block need is less than
                // deltaSize; basically means you found a better match
                if ((currBlockSize - blockSizeRequired) < deltaSize) {
                    // Set difference as new threshold to beat
                    deltaSize = currBlockSize - blockSizeRequired;

                    // Set best block to the current block (pointing to header)
                    best = ptr;

                    // If perfect size block found, exit loop
                    if (deltaSize == 0) {
                        perfectFit = 1;
                        break;
                    }
                }
            }
        }

        // Jump from current header to next header        
        ptr = (blk_hdr*)((char*) ptr + currBlockSize);
    }

    // We return NULL if we had not found a big enough block to allocate that
    // is available
    if (bigEnoughBlock == 0 || openBlockExists == 0) {
        return NULL;
    }

    // If a perfect match was not found, we want to split the block if possible
    if (perfectFit != 1) {        
        // Size of block before split (discounting a-bit and p-bit)
        int sizeOfOriginalBlock = (best->size_status) - 
                                  ((best->size_status) % 8);
        
        // Size of current block (block to be allocated) post-split is 
        // blockSizeRequired
        int sizeOfCurrentBlock = blockSizeRequired;
        
        // Size of new block (free) post-split
        int sizeOfNewBlock = sizeOfOriginalBlock - sizeOfCurrentBlock;
        
        // Pointer to the new header block of new (unused) block post-split
        blk_hdr* headerNewBlock = (blk_hdr*)((char*) best + blockSizeRequired);

        // New block should update p-bit to 1 since we know the previous block
        // post-split is being allocated, a-bit is 0
        headerNewBlock->size_status = sizeOfNewBlock + 2;
        
        // Pointer to the footer of the new block, which should be the footer 
        // of the old block before the split
        blk_hdr* footerNewBlock = (blk_hdr*)((char*) best + 
                                   sizeOfOriginalBlock - 4);
        
        // Update footer size_status
        footerNewBlock->size_status = sizeOfNewBlock;

        // Update the size_status of the block being allocated
        best->size_status = blockSizeRequired;               
    }
    
    // Update p-bit if necessary
    // If first_blk of heap memory we know p-bit = 1
    // We also can check p-bit from it being saved in a flag earlier
    if (best == first_blk || currBlockPBitFlag == 1) {
        best->size_status += 2;
    }

    // Set a-bit of the current block (the one we want to allocate) to 1
    best->size_status += 1;

    return (void*)((char*) best + 4);    
}


/* 
 * Function for freeing up a previously allocated block 
 * Argument - ptr: Address of the block to be freed up 
 * Returns 0 on success 
 * Returns -1 on failure 
 * Here is what this function should accomplish 
 * - Return -1 if ptr is NULL
 * - Return -1 if ptr is not 8 byte aligned or if the block is already freed
 * - Mark the block as free 
 * - Coalesce if one or both of the immediate neighbours are free 
 */                    
int Free_Mem(void *ptr) {                        
    // If ptr is NULL, exit method
    if (ptr == NULL) {
        return -1;
    }

    // If ptr is not 8 byte aligned, exit method
    if (((int) ptr) % 8 != 0) {
        return -1;
    }

    // Pointer to the header of the current payload
    blk_hdr* header = (blk_hdr*)((char*) ptr - 4);

    // If a-bit is 0 (block is already freed)
    // Represents status of the current block
    int sumLastTwoBits = (header->size_status) % 8;
    
    // These are the two possible cases for a+p bits where the a-bit is 0
    // Exit method in this case
    if (sumLastTwoBits == 0 || sumLastTwoBits == 2) {
        return -1;
    }

    // Size of current block (excluding a-bit and p-bit values)
    int currBlockSize = (header->size_status) - ((header->size_status) % 8);

    // At this point we know that sumLastTwoBits is either 1 or 3
    // Combination of p-bit = 0, a-bit = 1 or p-bit = 1, a-bit = 1

    // Flag if previous block is free, 1 = free, 0 = busy
    int previousBlockFree = 0;

    // Flag if next block is free, 1 = free, 0 = busy
    int nextBlockFree = 0;

    // Check previous and subsequent block if they are free
    
    // Check previous block
    // If previous block is empty, 
    // sumLastTwoBits can only be 1 (p-bit = 0, a-bit = 1)
    //
    // It cannot be 3 (the other case when a-bit is 1) because p-bit = 1, thus
    // meaning the previous block is busy.

    if (sumLastTwoBits == 1) {
        previousBlockFree = 1;
    }

    // Check next block

    // Pointer to the header of the next block
    blk_hdr* nextBlockHeader = (blk_hdr*)((char*) header + currBlockSize);

    // Sum of a-bit and p-bit of next header
    // NOTE: Watch out for end of heap, will produce value of 0.
    int nextBlockSumLastTwoBits = (nextBlockHeader->size_status) % 8;
    
    // If sum of a-bit and p-bit is equal to 0 or 2 (0 case should be 
    // impossible because if the current block was busy, the a-bit should
    // be 1, thus making 2 the only realistic sum of the a-bit and p-bit if
    // the subsequent block is free.
    if (nextBlockSumLastTwoBits == 2) {
        nextBlockFree = 1;
    }
    
    // CASE 1: The previous block is free, but the subsequent isn't
    if (previousBlockFree == 1 && nextBlockFree == 0) {
        // Get to the footer of the previous block and retrieve size of
        // previous block (discounting a-bit and p-bit)
        int sizeOfPreviousBlock = ((blk_hdr*)((char*) header - 4))->size_status;

        // Pointer to the header of the previous block
        blk_hdr* previousBlockHeader = (blk_hdr*)((char*) header - 
                                        sizeOfPreviousBlock);
        
        // Holds p-bit value of the previous block header
        // We know that since the previous block is free that the difference 
        // between the size_status and the size of the block is attributed to
        // only the p-bit
        int previousBlockPBit = (previousBlockHeader->size_status) - 
                                 sizeOfPreviousBlock;
        
        // Update previous header's size_status
        previousBlockHeader->size_status = sizeOfPreviousBlock +
                                           currBlockSize +
                                           previousBlockPBit;
        
        // Because coalesced block is free, we have to add a footer

        // Pointer to footer of current block
        blk_hdr* footerCurrentBlock = (blk_hdr*)((char*) previousBlockHeader +
                                                 sizeOfPreviousBlock +
                                                 currBlockSize - 4);

        // Set added footer size_status
        footerCurrentBlock->size_status = sizeOfPreviousBlock + currBlockSize;

        // Pointer to header of next block
        blk_hdr* headerNextBlock = (blk_hdr*)((char*) previousBlockHeader +
                                                      sizeOfPreviousBlock +
                                                      currBlockSize);

        // Update next header's p-bit to 0 if not already 0
        int nextBlockLastTwoBits = (headerNextBlock->size_status) % 8;
        if (nextBlockLastTwoBits == 2 || nextBlockLastTwoBits == 3) { 
            headerNextBlock->size_status -= 2;
        }
    } else if (previousBlockFree == 0 && nextBlockFree == 1) {
        // CASE 2: The previous block isn't free, but the subsequent one is
        // Pointer to the header of next block
        blk_hdr* headerNextBlock = (blk_hdr*)((char*) header + currBlockSize);

        // Next block's size
        int sizeOfNextBlock = headerNextBlock->size_status -
                              ((headerNextBlock->size_status) % 8);

        // Holds p-bit value of the current block header
        int currentBlockLastBits = (header->size_status) % 8;

        // Update current header's size_status
        header->size_status = currBlockSize + sizeOfNextBlock +
                              currentBlockLastBits - 1;

        // Because coalesced block is free, we have to add a footer

        // Pointer to footer of the next block
        blk_hdr* footerNextBlock = (blk_hdr*)((char*) header + currBlockSize
                                    + sizeOfNextBlock - 4);

        // Set added footer size_status
        footerNextBlock->size_status = currBlockSize + sizeOfNextBlock;

        // Pointer to header of next next block
        blk_hdr* headerNextNextBlock = (blk_hdr*)((char*) header
                                        + currBlockSize + sizeOfNextBlock);

        // Update next next block's p-bit to 0

        // Sum of a-bit and p-bit for the next next block
        int nextNextBlockLastTwoBits = (headerNextNextBlock->size_status) % 8;
        if (nextNextBlockLastTwoBits == 2 || nextNextBlockLastTwoBits == 3) {
            headerNextNextBlock->size_status -= 2;
        }
    // CASE 3: Both the previous block and the subsequent are free
    } else if (previousBlockFree == 1 && nextBlockFree == 1) {
        // Get to the footer of the previous block and retrieve size of
        // previous block (discounting a-bit and p-bit)
        int sizeOfPreviousBlock = ((blk_hdr*)((char*) header - 4))->size_status;

        // Pointer to the header of the previous block
        blk_hdr* previousBlockHeader = (blk_hdr*)((char*) header -
                                        sizeOfPreviousBlock);
        
        // Holds p-bit value of the previous block header
        int previousBlockPBit = (previousBlockHeader->size_status) - 
                                 sizeOfPreviousBlock;
        
        // Pointer to the header of the next block
        blk_hdr* headerNextBlock = (blk_hdr*)((char*) header + currBlockSize);

        // Next block's size
        int sizeOfNextBlock = headerNextBlock->size_status -
                              ((headerNextBlock->size_status) % 8);

        // Update previous header's size_status
        previousBlockHeader->size_status = sizeOfPreviousBlock +
                                           currBlockSize +
                                           sizeOfNextBlock +
                                           previousBlockPBit;

        // Because coalesced block is free, we have to add a footer
        blk_hdr* footerNextBlock = (blk_hdr*)((char*) previousBlockHeader +
                                               sizeOfPreviousBlock +
                                               currBlockSize +
                                               sizeOfNextBlock - 4);

        // Set added footer size_status
        footerNextBlock->size_status = sizeOfPreviousBlock + 
                                       currBlockSize + 
                                       sizeOfNextBlock;

        // Pointer to header of next next block
        blk_hdr* headerNextNextBlock = (blk_hdr*)((char*) previousBlockHeader +
                                                   sizeOfPreviousBlock +
                                                   currBlockSize +
                                                   sizeOfNextBlock);

        // Update next next block's p-bit to 0
        int nextNextBlockLastTwoBits = (headerNextNextBlock->size_status) % 8;
        if (nextNextBlockLastTwoBits == 2 || nextNextBlockLastTwoBits == 3) {
            headerNextNextBlock->size_status -= 2;
        }
    // CASE 4: Both the previous and subsequent blocks aren't free
    } else {
        // Pointer to the header of the next block
        blk_hdr* headerNextBlock = (blk_hdr*)((char*) header + currBlockSize);
        
        // Create footer of the current block
        blk_hdr* footerCurrentBlock = (blk_hdr*)((char*) header +
                                       currBlockSize - 4);

        // Update footer
        footerCurrentBlock->size_status = currBlockSize;

        // Update p-bit for subsequent block
        headerNextBlock->size_status -= 2;

        // Update a-bit for current block
        header->size_status -= 1;
    }
        
    return 0;
}

/*
 * Function used to initialize the memory allocator
 * Not intended to be called more than once by a program
 * Argument - sizeOfRegion: Specifies the size of the chunk which needs to be 
 * allocated
 * Returns 0 on success and -1 on failure 
 */                    
int Init_Mem(int sizeOfRegion)
{                         
    int pagesize;
    int padsize;
    int fd;
    int alloc_size;
    void* space_ptr;
    blk_hdr* end_mark;
    static int allocated_once = 0;
  
    if (0 != allocated_once) {
        fprintf(stderr, 
        "Error:mem.c: Init_Mem has allocated space during a previous call\n");
        return -1;
    }
    if (sizeOfRegion <= 0) {
        fprintf(stderr, "Error:mem.c: Requested block size is not positive\n");
        return -1;
    }

    // Get the pagesize
    pagesize = getpagesize();

    // Calculate padsize as the padding required to round up sizeOfRegion 
    // to a multiple of pagesize
    padsize = sizeOfRegion % pagesize;
    padsize = (pagesize - padsize) % pagesize;

    alloc_size = sizeOfRegion + padsize;

    // Using mmap to allocate memory
    fd = open("/dev/zero", O_RDWR);
    if (-1 == fd) {
        fprintf(stderr, "Error:mem.c: Cannot open /dev/zero\n");
        return -1;
    }
    space_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, 
                    fd, 0);
    if (MAP_FAILED == space_ptr) {
        fprintf(stderr, "Error:mem.c: mmap cannot allocate space\n");
        allocated_once = 0;
        return -1;
    }
  
     allocated_once = 1;

    // for double word alignement and end mark
    alloc_size -= 8;

    // To begin with there is only one big free block
    // initialize heap so that first block meets 
    // double word alignement requirement
    first_blk = (blk_hdr*) space_ptr + 1;
    end_mark = (blk_hdr*)((void*)first_blk + alloc_size);
  
    // Setting up the header
    first_blk->size_status = alloc_size;

    // Marking the previous block as busy
    first_blk->size_status += 2;

    // Setting up the end mark and marking it as busy
    end_mark->size_status = 1;

    // Setting up the footer
    blk_hdr *footer = (blk_hdr*) ((char*)first_blk + alloc_size - 4);
    footer->size_status = alloc_size;
  
    return 0;
}

/* 
 * Function to be used for debugging 
 * Prints out a list of all the blocks along with the following information i
 * for each block 
 * No.      : serial number of the block 
 * Status   : free/busy 
 * Prev     : status of previous block free/busy
 * t_Begin  : address of the first byte in the block (this is where the header starts) 
 * t_End    : address of the last byte in the block 
 * t_Size   : size of the block (as stored in the block header) (including the header/footer)
 */                     
void Dump_Mem() {                        
    int counter;
    char status[5];
    char p_status[5];
    char *t_begin = NULL;
    char *t_end = NULL;
    int t_size;

    blk_hdr *current = first_blk;
    counter = 1;

    int busy_size = 0;
    int free_size = 0;
    int is_busy = -1;

    fprintf(stdout, "************************************Block list***\
                    ********************************\n");
    fprintf(stdout, "No.\tStatus\tPrev\tt_Begin\t\tt_End\t\tt_Size\n");
    fprintf(stdout, "-------------------------------------------------\
                    --------------------------------\n");
  
    while (current->size_status != 1) {
        t_begin = (char*)current;
        t_size = current->size_status;
    
        if (t_size & 1) {
            // LSB = 1 => busy block
            strcpy(status, "Busy");
            is_busy = 1;
            t_size = t_size - 1;
        } else {
            strcpy(status, "Free");
            is_busy = 0;
        }

        if (t_size & 2) {
            strcpy(p_status, "Busy");
            t_size = t_size - 2;
        } else {
            strcpy(p_status, "Free");
        }

        if (is_busy) 
            busy_size += t_size;
        else 
            free_size += t_size;

        t_end = t_begin + t_size - 1;
    
        fprintf(stdout, "%d\t%s\t%s\t0x%08lx\t0x%08lx\t%d\n", counter, status, 
        p_status, (unsigned long int)t_begin, (unsigned long int)t_end, t_size);
    
        current = (blk_hdr*)((char*)current + t_size);
        counter = counter + 1;
    }

    fprintf(stdout, "---------------------------------------------------\
                    ------------------------------\n");
    fprintf(stdout, "***************************************************\
                    ******************************\n");
    fprintf(stdout, "Total busy size = %d\n", busy_size);
    fprintf(stdout, "Total free size = %d\n", free_size);
    fprintf(stdout, "Total size = %d\n", busy_size + free_size);
    fprintf(stdout, "***************************************************\
                    ******************************\n");
    fflush(stdout);

    return;
}
