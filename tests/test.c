#include <stdio.h>
#ifdef __linux__
#include <execinfo.h>
#include <signal.h>
#endif
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <stddef.h>
#include <unistd.h>
#include "../reorderLib.h"

#define NUM_OF_TEST_NODES	(10000)
#define TEST_LOOP			(100*NUM_OF_TEST_NODES)

#ifdef __linux__
void handler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d\n", sig);
  backtrace_symbols_fd(array, size, 2);
  exit(1);
}
#endif

void main(void) {
    time_t t;
	unsigned lba, numberOfBlocks;
    unsigned i;
    segment_t *tSeg, *cSeg, *nextSeg;
    tavl_node_t *cNode,*higherNode, *nextNode;
    unsigned currentLba, currentNB;
    unsigned totalSgDist, dist, totalTrackDist;
	unsigned rand_i;

#ifdef __linux__
	signal(SIGSEGV, handler);
#endif

    // srand(time(NULL)) initializes the random seed with the current time.
    srand((unsigned)time(&t));

    initCache(NUM_OF_TEST_NODES);
	getNumOfBlocks(&numberOfBlocks);
    printf("Initialized cache with NUM_OF_TEST_NODES, number of blocks:%d.\n", numberOfBlocks);

    // Test TAVL tree insertion and removal operation, with coherency management.
    // - Initialize the cache
    // - Get a segment from free pool
    // - Insert all NUM_OF_SEGMENTS segments into the tree, each with random key (0..1999) and number of block of (10..29)
    // - Invalidate(free) any segments that overlap with the current range
    // - Since the segment got inserted to TAVL tree, insert it to LRU too.
    // - Scan the Thread and make sure all segments are ordered and there is no overlap
    // - Traverse the Thread and remove each & every segment from TAVL and the list. Segment gets returned to free pool.
    // - Confirm that AVL tree, Thread and LRU are empty
    printf("Inserting all %d nodes.\n",NUM_OF_TEST_NODES);
    // Insert NUM_OF_SEGMENTS segments into the TAVL tree.
    for (i = 0; i < NUM_OF_TEST_NODES; i++) {
		// Make sure we get new LBA that does not overlap with any
		unsigned j=0;
		do {
			rand_i = (rand()&0xff<<24)+(rand()&0xff<<16)+(rand()&0xff<<8)+(rand()&0xff);
			lba = rand_i % numberOfBlocks;
			cNode=searchAvl(cacheMgmt.tavl.root, lba);
			if (NULL!=cNode) {
				printf("searchAvl(%d) returned cNode:%p with LBA range %d.\n", lba, cNode, cNode->pSeg->key);
			}
			j++;
			if (j>30) {
				printf("Could not add a new LBA.\n");
				assert(false);
			}
        } while (NULL!=cNode);

        printf("%dth LBA %d will be inserted\n", i, lba);
		// For the time being, use only 1 block.
		addLba(lba, 1);
		printf("%dth loop done\n", i);
    }

    // Scan the Thread and make sure all segments are ordered
    // Fetch the first segment in the Thread, one that is pointed by cacheMgmt.tavl.lowest.higher.
    cNode=cacheMgmt.tavl.lowest.higher;
    currentLba=0;
    currentNB=0;
    i=0;
    while (cNode!=&cacheMgmt.tavl.highest) {
        // Make sure this segment has an LBA that is equal or bigger than previous LBA + number of blocks
        assert(cNode->pSeg->key>=currentLba+currentNB);
        currentLba=cNode->pSeg->key;
        // (void)dumpPathToKey(cacheMgmt.tavl.root, currentLba);
        currentNB=cNode->pSeg->numberOfBlocks;
        cNode=cNode->higher;
        i++;
    }

	//dumpSgNodes();

	totalSgDist=0;
	totalTrackDist=0;
	i=0;
	while ((i<TEST_LOOP) && (cacheMgmt.tavl.root)) {
		printf("selectTargetFromCurrent()\n");
		cNode=selectTargetFromCurrent(&dist);
		totalSgDist+=dist;
		totalTrackDist+=abs(cNode->pSeg->track-cacheMgmt.currentTrack);
		printf("LBA %d to %d, SG %d to %d, track %d to %d\n", cacheMgmt.currentLba, cNode->pSeg->key, cacheMgmt.currentSg, cNode->pSeg->sg, cacheMgmt.currentTrack, cNode->pSeg->track);
		printf("completeTarget(%d)\n",cNode->pSeg->key);
		completeTarget(cNode->pSeg->key);
		// Add another random LBA.
		unsigned j=0;
		do {
			rand_i = (rand()&0xff<<24)+(rand()&0xff<<16)+(rand()&0xff<<8)+(rand()&0xff);
			lba = rand_i % numberOfBlocks;
			printf("searchAvl(%d)\n",lba);
			cNode=searchAvl(cacheMgmt.tavl.root, lba);
			if (NULL!=cNode) {
				printf("searchAvl(%d) returned cNode:%p with LBA range %d.\n", lba, cNode, cNode->pSeg->key);
			}
			j++;
			if (j>30) {
				printf("Could not add a new LBA after deleting.\n");
				assert(false);
			}
        } while (NULL!=cNode);
		// For the time being, use only 1 block.
		printf("addLba(%d)\n",lba);
		addLba(lba, 1);
		//printf("Target completed, going to the next...\n");
		printf("%dth loop done\n", i);
		i++;
	}

    // Traverse the Thread and remove each & every node from TAVL and the list. Node gets returned to free pool.
    // Fetch the first segment in the Thread, one that is pointed by cacheMgmt.tavl.lowest.higher.
    printf("Removing all nodes in the Thread\n");
    cNode=cacheMgmt.tavl.lowest.higher;
    while (cNode!=&cacheMgmt.tavl.highest) {
        // Remove this node
        nextNode=cNode->higher;
        freeNode(cNode->pSeg);
        cNode=nextNode;
    }

    // Traverse the LRU and dump any remaining segments.
    printf("Dumping any segments in LRU, there should be none left\n");
    tSeg=cacheMgmt.lru.head.next;
    i=0;
    while (tSeg!=&cacheMgmt.lru.tail) {
        printf("%dth seg %p in the LRU, LBA range [%d..%d]\n", i, tSeg, tSeg->key, tSeg->key+tSeg->numberOfBlocks);
        // Remove this node
        tSeg=tSeg->next;
        i++;
    }

    // Confirm that AVL tree, Thread and LRU are empty
    printf("Checking the tree is empty\n");
    assert(NULL==cacheMgmt.tavl.root);
    printf("Checking the thread is empty\n");
    assert(cacheMgmt.tavl.lowest.higher==&cacheMgmt.tavl.highest);
    assert(cacheMgmt.tavl.highest.lower==&cacheMgmt.tavl.lowest);
    printf("Checking the LRU is empty\n");
    assert(cacheMgmt.lru.head.next==&cacheMgmt.lru.tail);
    assert(cacheMgmt.lru.tail.prev==&cacheMgmt.lru.head);
    printf("Checking all SG trees are empty\n");
    for (i = 0; i < NUMBER_OF_SG; i++) {
        assert(pSgTavl[i].root==NULL);
        assert(pSgTavl[i].lowest.higher==&pSgTavl[i].highest);
        assert(pSgTavl[i].highest.lower==&pSgTavl[i].lowest);
    }

    printf("Test successful. Total time distance:%d. Total tracks traveled:%d.\n", totalSgDist, totalTrackDist);
}
