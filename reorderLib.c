#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <math.h>
#include "reorderLib.h"

//-----------------------------------------------------------
// Global variables
//-----------------------------------------------------------
segment_t       *pSegmentPool;
tavl_node_t     *pNodePool;
tavl_t 			*pSgTavl;
cManagement_t   cacheMgmt;
dpReorder_t		dpReorder;
unsigned		*pInvSeekProfile;

//-----------------------------------------------------------
// Functions
//-----------------------------------------------------------
void initSegment(segment_t *pSeg) {
    pSeg->prev = NULL;
    pSeg->next = NULL;
    pSeg->key = 0;
    pSeg->numberOfBlocks = 0;
    pSeg->sg = 0;
    pSeg->track = 0;
	pSeg->reordered=false;
}

void initNode(tavl_node_t *pNode) {
    pNode->left = NULL;
    pNode->right = NULL;
    pNode->lower = NULL;
    pNode->higher = NULL;
    pNode->height = 1;
}

void pushToTail(segment_t *pSeg, segList_t *pList) {
    segment_t *pPrev = pList->tail.prev;
    pPrev->next=pSeg;
    pSeg->prev=pPrev;
    pList->tail.prev=pSeg;
    pSeg->next=&(pList->tail);
}

void removeFromList(segment_t *pSeg) {
    segment_t *pPrev = pSeg->prev;
    segment_t *pNext = pSeg->next;
    pPrev->next=pNext;
    pNext->prev=pPrev;
    pSeg->prev=NULL;
    pSeg->next=NULL;
}

segment_t *popFromHead(segList_t *pList) {
    segment_t *pSeg=pList->head.next;
    if (&pList->tail==pSeg) {
        return NULL;
    }
    removeFromList(pSeg);
    return pSeg;
}

void removeFromThread(tavl_node_t *pNode) {
    tavl_node_t *pLower = pNode->lower;
    tavl_node_t *pHigher = pNode->higher;
    pLower->higher=pHigher;
    pHigher->lower=pLower;
    pNode->lower=NULL;
    pNode->higher=NULL;
}

void insertBefore(tavl_node_t *pNode, tavl_node_t *pTarget) {
    tavl_node_t *pLower = pTarget->lower;
    pLower->higher=pNode;
    pTarget->lower=pNode;
    pNode->lower=pLower;
    pNode->higher=pTarget;
}

void insertAfter(tavl_node_t *pNode, tavl_node_t *pTarget) {
    tavl_node_t *pHigher = pTarget->higher;
    pHigher->lower=pNode;
    pTarget->higher=pNode;
    pNode->lower=pTarget;
    pNode->higher=pHigher;
}

void insertPriorTo(segment_t *pSeg, segment_t *pTarget) {
    segment_t *pPrev = pTarget->prev;
    pPrev->next=pSeg;
    pTarget->prev=pSeg;
    pSeg->prev=pPrev;
    pSeg->next=pTarget;
}

void insertNextTo(segment_t *pSeg, segment_t *pTarget) {
    segment_t *pNext = pTarget->next;
    pNext->prev=pSeg;
    pTarget->next=pSeg;
    pSeg->prev=pTarget;
    pSeg->next=pNext;
}

unsigned avlHeight(tavl_node_t *head) {
    if (NULL == head) {
        return 0;
    }
    return head->height;
}

tavl_node_t *rightRotation(tavl_node_t *head) {
	assert(NULL!=head);
	assert(NULL!=head->left);
    tavl_node_t *newHead = head->left;
	assert(NULL!=newHead);
    head->left = newHead->right;
    newHead->right = head;
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    newHead->height = 1 + MAX(avlHeight(newHead->left), avlHeight(newHead->right));
    return newHead;
}

tavl_node_t *leftRotation(tavl_node_t *head) {
	assert(NULL!=head);
	assert(NULL!=head->right);
    tavl_node_t *newHead = head->right;
	assert(NULL!=newHead);
    head->right = newHead->left;
    newHead->left = head;
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    newHead->height = 1 + MAX(avlHeight(newHead->left), avlHeight(newHead->right));
    return newHead;
}

tavl_node_t *insertNode(tavl_node_t *head, tavl_node_t *x) {
    if (NULL == head) {
        return x;
    }
    if (x->pSeg->key < head->pSeg->key) {
        head->left = insertNode(head->left, x);
    } else if (x->pSeg->key > head->pSeg->key) {
        head->right = insertNode(head->right, x);
    }
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    int bal = avlHeight(head->left) - avlHeight(head->right);
    if (bal > 1) {
        if (x->pSeg->key < head->left->pSeg->key) {
            return rightRotation(head);
        } else {
            head->left = leftRotation(head->left);
            return rightRotation(head);
        }
    } else if (bal < -1) {
        if (x->pSeg->key > head->right->pSeg->key) {
            return leftRotation(head);
        } else {
            head->right = rightRotation(head->right);
            return leftRotation(head);
        }
    }

    return head;
}

tavl_node_t *removeNode(tavl_node_t *head, segment_t *x) {
    if (NULL == head) {
        return NULL;
    }
    if (x->key < head->pSeg->key) {
        // if the node belongs to the left, traverse through left.
        head->left = removeNode(head->left, x);
    } else if (x->key > head->pSeg->key) {
        // if the node belongs to the right, traverse through right.
        head->right = removeNode(head->right, x);
    } else {
        // if the node is the tree, copy the key of the node that is just bigger than the key being removed & remove the node from the right
        tavl_node_t *r = head->right;
        if (NULL == head->right) {
            tavl_node_t *l = head->left;
            // Remove from the thread.
            removeFromThread(head);
            head = l;
        } else if (NULL == head->left) {
            // Remove from the thread.
            removeFromThread(head);
            head = r;
        } else {
            // Instead of traversing the tree, use the thread to find the right next one.
            r = (tavl_node_t *)(head->higher);
            // Swap the segment between head and r.
            // The segment pointed by r will be preserved in the thread.
            // The segment pointed by head will be removed from the thread when r node gets removed later.
            segment_t *pHeadSeg = head->pSeg;
            segment_t *pRSeg = r->pSeg;
            head->pSeg = pRSeg;
            r->pSeg = pHeadSeg;
            pHeadSeg->pNode = (void *)r;
            pRSeg->pNode = (void *)head;

            head->right = removeNode(head->right, r->pSeg);
        }
    }
    // unless the tree is empty, check the balance and rebalance the tree before traversing back
    if (NULL == head) {
        return NULL;
    }
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    int bal = avlHeight(head->left) - avlHeight(head->right);
    if (bal > 1) {
        if (avlHeight(head->left->left) >= avlHeight(head->left->right)) {
            return rightRotation(head);
        } else {
            head->left = leftRotation(head->left);
            return rightRotation(head);
        }
    } else if (bal < -1 ) {
        if (avlHeight(head->right->right) >= avlHeight(head->right->left)) {
            return leftRotation(head);
        } else {
            head->right = rightRotation(head->right);
            return leftRotation(head);
        }
    }

    return head;
}

tavl_node_t *removeNodeSub(tavl_node_t *head, segment_t *x) {
    if (NULL == head) {
        return NULL;
    }
    if (x->key < head->pSeg->key) {
        // if the node belongs to the left, traverse through left.
        head->left = removeNodeSub(head->left, x);
    } else if (x->key > head->pSeg->key) {
        // if the node belongs to the right, traverse through right.
        head->right = removeNodeSub(head->right, x);
    } else {
        // if the node is the tree, copy the key of the node that is just bigger than the key being removed & remove the node from the right
        tavl_node_t *r = head->right;
        if (NULL == head->right) {
            tavl_node_t *l = head->left;
            // Remove from the thread.
            removeFromThread(head);
            head = l;
        } else if (NULL == head->left) {
            // Remove from the thread.
            removeFromThread(head);
            head = r;
        } else {
            // Instead of traversing the tree, use the thread to find the right next one.
            r = (tavl_node_t *)(head->higher);
            // Swap the segment between head and r.
            // The segment pointed by r will be preserved in the thread.
            // The segment pointed by head will be removed from the thread when r node gets removed later.
            segment_t *pHeadSeg = head->pSeg;
            segment_t *pRSeg = r->pSeg;
            head->pSeg = pRSeg;
            r->pSeg = pHeadSeg;
            pHeadSeg->pNodeSub = (void *)r;
            pRSeg->pNodeSub = (void *)head;

            head->right = removeNodeSub(head->right, r->pSeg);
        }
    }
    // unless the tree is empty, check the balance and rebalance the tree before traversing back
    if (NULL == head) {
        return NULL;
    }
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    int bal = avlHeight(head->left) - avlHeight(head->right);
    if (bal > 1) {
        if (avlHeight(head->left->left) >= avlHeight(head->left->right)) {
            return rightRotation(head);
        } else {
            head->left = leftRotation(head->left);
            return rightRotation(head);
        }
    } else if (bal < -1 ) {
        if (avlHeight(head->right->right) >= avlHeight(head->right->left)) {
            return leftRotation(head);
        } else {
            head->right = rightRotation(head->right);
            return leftRotation(head);
        }
    }
    return head;
}

tavl_node_t *searchAvl(tavl_node_t *head, unsigned key) {
    if (NULL == head) {
        return NULL;
    }
    unsigned k = head->pSeg->key;
    if (key == k) {
        return head;
    }
    if (k > key) {
        return searchAvl(head->left, key);
    }
    if (k < key) {
        return searchAvl(head->right, key);
    }
}

tavl_node_t *searchTavl(tavl_node_t *head, unsigned lba) {
    if (NULL == head) {
        return NULL;
    }
    unsigned k = head->pSeg->key;
    if (lba == k) {
        return head;
    }
    if (k > lba) {
        if (NULL==head->left) {
            return (tavl_node_t *)(head->lower);
        }
        return searchTavl(head->left, lba);
    }
    if (k < lba) {
        if (NULL==head->right) {
            return head;
        }
        return searchTavl(head->right, lba);
    }
}

/**
 *  @brief  Inserts the given node into the given TAVL tree that is NOT empty.
 *          In other words,
 *          1. inserts the given node into AVL tree
 *          2. inserts the given node into the Thread
 *  @param  tavl_node_t *head - root of the tree, 
 *          tavl_node_t *x - pointer to the node to be inserted
 *  @return New root of the tree
 */
tavl_node_t *_insertToTavl(tavl_node_t *head, tavl_node_t *x) {
	assert(NULL!=x);
	assert(NULL!=x->pSeg);
	assert(NULL!=head);
	assert(NULL!=head->pSeg);
    if (x->pSeg->key < head->pSeg->key) {
        if (NULL==head->left) {
            insertBefore(x, head);
            head->left = x;
        } else {
            head->left = _insertToTavl(head->left, x);
        }
    } else if (x->pSeg->key > head->pSeg->key) {
        if (NULL==head->right) {
            insertAfter(x, head);
            head->right = x;
        } else {
            head->right = _insertToTavl(head->right, x);
        }
    }
    head->height = 1 + MAX(avlHeight(head->left), avlHeight(head->right));
    int bal = avlHeight(head->left) - avlHeight(head->right);
    if (bal > 1) {
        if (x->pSeg->key < head->left->pSeg->key) {
            return rightRotation(head);
        } else {
            head->left = leftRotation(head->left);
            return rightRotation(head);
        }
    } else if (bal < -1) {
        if (x->pSeg->key > head->right->pSeg->key) {
            return leftRotation(head);
        } else {
			if (NULL==head->right->left) {
				(void)dumpPathToKey(head, head->right->pSeg->key);
			}
            head->right = rightRotation(head->right);
			if (NULL==head->right) {
				(void)dumpPathToKey(head, head->left->pSeg->key);
			}
            return leftRotation(head);
        }
    }
    return head;
}

tavl_node_t *insertToTavl(tavl_t *pTavl, tavl_node_t *x) {
	assert(NULL!=pTavl);
	assert(NULL!=x);
    pTavl->active_nodes++;
    if (NULL == pTavl->root) {
        pTavl->lowest.higher=x;
        x->lower=&pTavl->lowest;
        pTavl->highest.lower=x;
        x->higher=&pTavl->highest;
        return x;
    } else {
        return _insertToTavl(pTavl->root, x);
    }
}

void freeNode(segment_t *x) {
	unsigned sg=x->sg;
	tavl_node_t	*tNode;

#if (SELECTED_REORDERING==SHORTEST_DIST_WITHIN_RANGE)
	// We want to keep track of LBA range for the shortest distance search.
	// When we complete & free a node that happens to be dpReorder.lbaRangeFirst, we advance dpReorder.lbaRangeFirst to the next node in the thread.
	segment_t *tSeg=dpReorder.lbaRangeFirst;
	if (tSeg==x) {
		if (1==cacheMgmt.tavl.active_nodes) {
			printf("freeNode(%p), Setting dpReorder.lbaRangeFirst to NULL as it is empty now.\n", x);
			// If this was the last node in the system, we initialize both first/last to NULL
			dpReorder.lbaRangeFirst=NULL;
			dpReorder.lbaRangeLast=NULL;
			// In SHORTEST_DIST_WITHIN_RANGE, dpReorder.lastLba is just the LBA of last completed
			dpReorder.lastLba=x->key;
		} else {
			// Since we have at least one more node in the system, traverse the thread and find the first one
			tNode=((tavl_node_t *)(tSeg->pNode))->higher;
			assert(NULL!=tNode);
			if (&cacheMgmt.tavl.highest==tNode) {
				// Handle wraparound - when we completed sweeping till the last LBA, start from the lowest LBA.
				dpReorder.lbaRangeFirst=cacheMgmt.tavl.lowest.higher->pSeg;
				printf("freeNode(%p), After completing LBA:%u, wrapping around dpReorder.lbaRangeFirst to LBA:%u, range start track:%u.\n", x, x->key, dpReorder.lbaRangeFirst->key, dpReorder.lbaRangeFirst->track);
			} else {
				dpReorder.lbaRangeFirst=tNode->pSeg;
				printf("freeNode(%p), After completing LBA:%u, advancing dpReorder.lbaRangeFirst to LBA:%u, range start track:%u.\n", x, x->key, dpReorder.lbaRangeFirst->key, dpReorder.lbaRangeFirst->track);
				printf("dpReorder.lbaRangeFirst(%p)->track:%u.\n", dpReorder.lbaRangeFirst, dpReorder.lbaRangeFirst->track);
			}
		}
	} else {
		printf("freeNode(%p), Not advancing dpReorder.lbaRangeFirst with LBA:%u as freed node has higher LBA:%u, range start track:%u.\n", x, dpReorder.lbaRangeFirst->key, x->key, dpReorder.lbaRangeFirst->track);
	}
	

#elif (SELECTED_REORDERING==PATH_BUILDING_FROM_LBA)
	// We will remove this segment from reordered list. Decrement the total.
	dpReorder.totalReordered--;

	// We want to keep track of LBA range for the entries in reordered list - dpReorder.reordered list,
	// such that we can search from dpReorder.lbaRangeFirst to find the first node that is not reordered yet.
	// This is so that any nodes that have lower LBA than dpReorder.lbaRangeFirst will not be entered into reordered list.
	// When we complete & free a node that happens to be dpReorder.lbaRangeFirst, we advance dpReorder.lbaRangeFirst to the next reordered node.
	segment_t *tSeg=dpReorder.lbaRangeFirst;
	if (tSeg==x) {
		if (0==dpReorder.totalReordered) {
			// If this was the last reordered node, we initialize both first/last to NULL
			dpReorder.lbaRangeFirst=NULL;
			dpReorder.lbaRangeLast=NULL;
		} else {
			// Since we have at least one reordered entry in reordered list, traverse the thread and find the first one
			while (!tSeg->reordered) {
				tNode=((tavl_node_t *)(tSeg->pNode))->higher;
				assert(NULL!=tNode);
			}
			assert(&cacheMgmt.tavl.highest!=tNode);
			dpReorder.lbaRangeFirst=tNode->pSeg;
		}
	}
#endif // (SELECTED_REORDERING==PATH_BUILDING_FROM_LBA)

    removeFromList(x);
    pushToTail(x, &cacheMgmt.free);

    // Remove the node from TAVL tree & return the new root
    cacheMgmt.tavl.active_nodes--;
    cacheMgmt.tavl.root=removeNode(cacheMgmt.tavl.root, x);

    pSgTavl[sg].active_nodes--;
    pSgTavl[sg].root=removeNodeSub(pSgTavl[sg].root, x);
}

tavl_node_t *dumpPathToKey(tavl_node_t *head, unsigned lba) {
    if (NULL == head) {
        printf("Unknown Key\n");
        return NULL;
    }
    unsigned k = head->pSeg->key;
    if (lba == k) {
        printf("(%d..%d)(%d)\n", lba, lba+head->pSeg->numberOfBlocks,head->height);
        return head;
    }
    if (k > lba) {
        if (NULL==head->left) {
            printf("Unknown Key\n");
            return (tavl_node_t *)(head->lower);
        }
        printf("l(%d)-",head->left->height);
        return dumpPathToKey(head->left, lba);
    }
    if (k < lba) {
        if (NULL==head->right) {
            printf("Unknown Key\n");
            return head;
        }
        printf("r(%d)-",head->right->height);
        return dumpPathToKey(head->right, lba);
    }
}

void dumpOneSgNodes(unsigned sg)
{
    unsigned j;
	tavl_node_t *tNode;
	if (NULL==pSgTavl[sg].root) {
		return;
	}
	j=0;
	tNode=pSgTavl[sg].lowest.higher;
	printf("SG[%d] : ", sg);
	while (tNode!=&pSgTavl[sg].highest) {
		printf("%d(%d,%d) ", tNode->pSeg->key, tNode->pSeg->sg, tNode->pSeg->track);
		tNode=tNode->higher;
		j++;
	}
	printf(", total nodes : %d(active_nodes:%d).\n", j, pSgTavl[sg].active_nodes);
}


void dumpSgNodes(void)
{
    unsigned i, j;
	tavl_node_t *tNode;
	printf("dumpSgNodes(), dumping all nodes in all SG.\n");
	for (i=0;i<NUMBER_OF_SG;i++) {
		dumpOneSgNodes(i);
	}
}

void tavlSanityCheck(tavl_t *pTavl) {
	tavl_node_t *cNode,*searchedNode;
	segment_t 	*tSeg;
    unsigned currentLba, currentNB;
    unsigned i;

    cNode=pTavl->lowest.higher;
	assert(NULL!=cNode);
    currentLba=0;
    currentNB=0;
    i=0;
    while (cNode!=&pTavl->highest) {
        // Make sure this segment has an LBA that is equal or bigger than previous LBA + number of blocks
        assert(cNode->pSeg->key>=currentLba+currentNB);
		tSeg=cNode->pSeg;
		assert(NULL!=cNode);
		assert(tSeg->pNode==(void *)cNode);
        currentLba=cNode->pSeg->key;
        (void)dumpPathToKey(pTavl->root, currentLba);
		searchedNode=searchAvl(pTavl->root, currentLba);
		if (searchedNode==NULL) {
			printf("tavlSanityCheck() but could not find the LBA %d.\n", currentLba);
			assert(searchedNode!=NULL);
		}
        i++;
        currentNB=cNode->pSeg->numberOfBlocks;
        cNode=cNode->higher;
    }
	assert(pTavl->active_nodes==i);
}

void tavlSanityCheckSub(tavl_t *pTavl) {
	tavl_node_t *cNode,*searchedNode;
	segment_t 	*tSeg;
    unsigned currentLba, currentNB;
    unsigned i;

    cNode=pTavl->lowest.higher;
	assert(NULL!=cNode);
    currentLba=0;
    currentNB=0;
    i=0;
    while (cNode!=&pTavl->highest) {
        // Make sure this segment has an LBA that is equal or bigger than previous LBA + number of blocks
        assert(cNode->pSeg->key>=currentLba+currentNB);
		tSeg=cNode->pSeg;
		assert(NULL!=cNode);
		assert(tSeg->pNodeSub==(void *)cNode);
        currentLba=cNode->pSeg->key;
        (void)dumpPathToKey(pTavl->root, currentLba);
		searchedNode=searchAvl(pTavl->root, currentLba);
		if (searchedNode==NULL) {
			printf("tavlSanityCheckSub() but could not find the LBA %d.\n", currentLba);
			assert(searchedNode!=NULL);
		}
        i++;
        currentNB=cNode->pSeg->numberOfBlocks;
        cNode=cNode->higher;
    }
	assert(pTavl->active_nodes==i);
}

bool tavlHeightCheck(tavl_node_t *head) {
    if (NULL == head) {
        return true;
    }
	if ((NULL==head->left) && (NULL==head->right)) {
		if (head->height != 1) {
			printf("tavlHeightCheck(%p) with key:%d. height:%d should have been 1\n", head, head->pSeg->key, head->height);
			return false;
		}
		return true;
	}
    if (head->height != 1 + MAX(avlHeight(head->left), avlHeight(head->right))) {
		printf("tavlHeightCheck(%p) height:%d, key:%d, left height:%d, right height:%d\n", head, head->height, head->pSeg->key, avlHeight(head->left), avlHeight(head->right));
		assert(head->height == 1 + MAX(avlHeight(head->left), avlHeight(head->right)));
	}

    int bal = avlHeight(head->left) - avlHeight(head->right);
    if ((bal > 1)||(bal<-1)) {
		printf("tavlHeightCheck(%p) height:%d, key:%d, left height:%d, right height:%d\n", head, head->height, head->pSeg->key, avlHeight(head->left), avlHeight(head->right));
		assert((bal <= 1)&&(bal>=-1));
	}
	if (head->left) {
		if (false==tavlHeightCheck(head->left)) {
			return false;
		}
	}
	if (head->right) {
		if (false==tavlHeightCheck(head->right)) {
			return false;
		}
	}
	return true;
}


//-----------------------------------------------------------
// Public Functions, used by python lib
//-----------------------------------------------------------
void getNumOfBlocks(unsigned *pNumberOfBlocks) {
	// Return the number of blocks
	*pNumberOfBlocks=NUMBER_OF_BLOCKS;
}

void getNumOfSgs(unsigned *pNumberOfSgs) {
	// Return the number of SGs
	*pNumberOfSgs=NUMBER_OF_SG;
}

void getNumOfTracks(unsigned *pNumberOfTracks) {
	// Return the number of tracks
	*pNumberOfTracks=NUMBER_OF_TRACKS;
}

void getPhyFromLba(unsigned lba, unsigned *pSg, unsigned *pTrack)
{
	unsigned temp_sg, temp_track;
	temp_sg = lba / BLOCKS_PER_SG;
	temp_track = temp_sg / NUMBER_OF_SG;
	*pTrack = temp_track;
	*pSg = ((temp_sg % NUMBER_OF_SG) + (TRACK_SKEW * temp_track)) % NUMBER_OF_SG;
}

void addLba(unsigned lba, unsigned num_of_blocks) {
	segment_t 	*tSeg;
	tavl_node_t *cNode;

	// Search cacheMgmt.tavl.root tree to find if there are nodes with overlap.
	// Assert if there is an overlap.
	cNode=searchAvl(cacheMgmt.tavl.root, lba);
	assert(NULL==cNode);

	// Pop from free pool
	tSeg=popFromHead(&cacheMgmt.free);
	assert(NULL!=tSeg);

	initSegment(tSeg);
	initNode(tSeg->pNode);
	initNode(tSeg->pNodeSub);

	// Calculate physical location and set to the segment
	tSeg->key=lba;
	tSeg->numberOfBlocks=num_of_blocks;
	getPhyFromLba(lba, &tSeg->sg, &tSeg->track);

	// Insert into cacheMgmt.tavl.root tree.
	cacheMgmt.tavl.root = insertToTavl(&cacheMgmt.tavl, (tavl_node_t *)(tSeg->pNode));

	// Insert into pSgTavl[sg] tree.
	pSgTavl[tSeg->sg].root = insertToTavl(&pSgTavl[tSeg->sg], (tavl_node_t *)(tSeg->pNodeSub));

	// Push to LRU tail
	pushToTail(tSeg, &cacheMgmt.lru);
}

void getDistance(unsigned startSg, unsigned startTrack, unsigned targetSg, unsigned targetTrack, unsigned *pDistance) {
	unsigned 	sgDiff, track_diff, track_range_top, track_range_bottom;

	// Adjust targetSg so that it is higner than startSg.
	// If startSg==targetSg, targetSg will be (NUMBER_OF_SG+startSg) implying that it will take a revolution from startSg to targetSg
	while (startSg >= targetSg) {
		targetSg+=NUMBER_OF_SG;
	}
	sgDiff=targetSg-startSg;
	assert(sgDiff>0);
	assert(sgDiff<=NUMBER_OF_SG);

	while (true) {
		track_diff=pInvSeekProfile[sgDiff];
		track_range_top=startTrack+track_diff;
		track_range_top=MIN(track_range_top, NUMBER_OF_TRACKS-1);
		track_range_bottom=(startTrack>=track_diff)?startTrack-track_diff:0;

		if ((targetTrack<=track_range_top) && (targetTrack>=track_range_bottom)) {
			break;
		}
		sgDiff+=NUMBER_OF_SG;
		if (sgDiff>=SEEK_TIME_LIMIT) {
			printf("startSg:%u, startTrack:%u, targetSg:%u, targetTrack:%u, sgDiff:%u, pInvSeekProfile[%u]:%u.\n", startSg, startTrack, targetSg, targetTrack, sgDiff, sgDiff-NUMBER_OF_SG, pInvSeekProfile[sgDiff-NUMBER_OF_SG]);
			assert(sgDiff<SEEK_TIME_LIMIT);
		}
	}
	*pDistance=sgDiff;
}

/**
 *  @brief  Search the target from the given SG and track.
 *			Return the LBA of the target & the distance (in number of SGs).
 *  @param  unsigned startLba - starting LBA, unsigned startSg - starting SG, unsigned startTrack- starting track, 
 *			unsigned *pDistance - pointer for the distance
 *  @return pointer of the node
 */
tavl_node_t *selectTarget(unsigned startLba, unsigned startSg, unsigned startTrack, unsigned *pDistance) {
    unsigned 	i;
	unsigned 	target_sg, track_diff, track_range_top, track_range_bottom;
	unsigned	cTrack;
	tavl_node_t *cNode,*higherNode;
	bool		traversingHigher, traversingLower;

	target_sg=startSg;
	for (i=0; i<SEEK_TIME_LIMIT; i++) {
		// i is for indexing pInvSeekProfile[]
		// target_sg for indexing pSgTavl[]
		if (NULL!=pSgTavl[target_sg].root) {
			// If this SG has nodes, search the tree
			track_diff=pInvSeekProfile[i];
			track_range_top=startTrack+track_diff;
			track_range_top=MIN(track_range_top, NUMBER_OF_TRACKS-1);
			track_range_bottom=(startTrack>=track_diff)?startTrack-track_diff:0;

			// Start searching the tree for startLba
			cNode=searchTavl(pSgTavl[target_sg].root, startLba);
			// As we checked this tree being not empty earlier, searchTavl cannot return NULL
			assert(NULL!=cNode);
			// searchTavl() returns a node that has equal or smaller LBA than startLba. (it could also be pSgTavl[target_sg].lowest)
			// So start comparison from the next node.
			higherNode=cNode->higher;
			assert(NULL!=higherNode);
			traversingHigher=traversingLower=true;
			do {
				if (traversingLower) {
					if (cNode!=&pSgTavl[target_sg].lowest) {
						assert(NULL!=cNode->pSeg);
						cTrack=cNode->pSeg->track;
						if (cTrack>=track_range_bottom) {
							if (cTrack<=track_range_top) {
								// Found one in the track range.
								*pDistance=i;
								return cNode;
							} else {
								// Hit the lowest without finding.
								traversingLower=false;
							}
						}
						cNode=cNode->lower;
						assert(NULL!=cNode);
					} else {
						traversingLower=false;
					}
				}
				if (traversingHigher) {
					if (higherNode!=&pSgTavl[target_sg].highest) {
						assert(NULL!=higherNode->pSeg);
						cTrack=higherNode->pSeg->track;
						if (cTrack>=track_range_bottom) {
							if (cTrack<=track_range_top) {
								// Found one in the track range.
								*pDistance=i;
								return higherNode;
							} else {
								// Exhausted the range without finding.
								traversingHigher=false;
							}
						}
						higherNode=higherNode->higher;
						assert(NULL!=higherNode);
					} else {
						traversingHigher=false;
					}
				}
			} while (traversingHigher || traversingLower);
        }

		target_sg++;
		if (target_sg>=NUMBER_OF_SG) {
			target_sg-=NUMBER_OF_SG;
		}
	}
	*pDistance=0xffff;
	return NULL;
}

#if (SELECTED_REORDERING==SHORTEST_DIST_WITHIN_RANGE)
/**
 *  @brief  Search the target from the given SG and track.
 *			Return the LBA of the target & the distance (in number of SGs).
 *  @param  unsigned startLba - starting LBA, unsigned startSg - starting SG, unsigned startTrack- starting track, 
 *			unsigned *pDistance - pointer for the distance
 *  @return pointer of the node
 */
tavl_node_t *selectTargetWithinRange(unsigned startLba, unsigned startSg, unsigned startTrack, unsigned *pDistance) {
    unsigned 	i;
	unsigned 	target_sg, track_diff, track_range_top, track_range_bottom, track_limit_top, track_limit_bottom;
	unsigned	cTrack;
	tavl_node_t *cNode,*higherNode;
	bool		traversingHigher, traversingLower;

	// If there is nothing set in LBA range, find the LBA range by using dpReorder.lastLba.
	if (NULL==dpReorder.lbaRangeFirst) {
		assert(cacheMgmt.tavl.root);
		// Start searching the tree from dpReorder.lastLba
		cNode=searchTavl(cacheMgmt.tavl.root, dpReorder.lastLba);
		// As we checked this tree being not empty earlier, searchTavl cannot return NULL
		assert(NULL!=cNode);
		// searchTavl() returns a node that has equal or smaller LBA than dpReorder.lastLba. (it could also be cacheMgmt.tavl.lowest)
		// So start comparison from the next node.
		cNode=cNode->higher;
		assert(NULL!=cNode);
		dpReorder.lbaRangeFirst=cNode->pSeg;
		dpReorder.lbaRangeLast=cNode->pSeg;
		printf("selectTargetWithinRange(), dpReorder.lbaRangeFirst was NULL, searched and found with dpReorder.lastLba:%u to get dpReorder.lbaRangeFirst->key:%u, track:%u.\n", dpReorder.lastLba, dpReorder.lbaRangeFirst->key, dpReorder.lbaRangeFirst->track);
	}
	track_limit_bottom=(dpReorder.lbaRangeFirst->track > cacheMgmt.maxBacktrack)? dpReorder.lbaRangeFirst->track-cacheMgmt.maxBacktrack: 0;
	printf("selectTargetWithinRange(), dpReorder.lbaRangeFirst(%p)->track:%u, cacheMgmt.maxBacktrack:%u, track_limit_bottom:%u.\n", dpReorder.lbaRangeFirst, dpReorder.lbaRangeFirst->track, cacheMgmt.maxBacktrack, track_limit_bottom);
	track_limit_top=MIN(startTrack+cacheMgmt.maxTrackRange, NUMBER_OF_TRACKS-1);

	target_sg=startSg;
	for (i=0; i<SEEK_TIME_LIMIT; i++) {
		// i is for indexing pInvSeekProfile[]
		// target_sg for indexing pSgTavl[]
		if (NULL!=pSgTavl[target_sg].root) {
			// If this SG has nodes, search the tree
			track_diff=pInvSeekProfile[i];
			track_range_top=startTrack+track_diff;
			track_range_top=MIN(track_range_top, track_limit_top);
			track_range_bottom=(startTrack>=track_diff)?startTrack-track_diff:0;
			track_range_bottom=MAX(track_range_bottom, track_limit_bottom);

			// Start searching the tree for startLba
			cNode=searchTavl(pSgTavl[target_sg].root, startLba);
			// As we checked this tree being not empty earlier, searchTavl cannot return NULL
			assert(NULL!=cNode);
			// searchTavl() returns a node that has equal or smaller LBA than startLba. (it could also be pSgTavl[target_sg].lowest)
			// So start comparison from the next node.
			higherNode=cNode->higher;
			assert(NULL!=higherNode);
			traversingHigher=traversingLower=true;
			do {
				if (traversingLower) {
					if (cNode!=&pSgTavl[target_sg].lowest) {
						assert(NULL!=cNode->pSeg);
						cTrack=cNode->pSeg->track;
						if (cTrack>=track_range_bottom) {
							if (cTrack<=track_range_top) {
								// Found one in the track range.
								*pDistance=i;
								return cNode;
							} else {
								// Hit the lowest without finding.
								traversingLower=false;
							}
						}
						cNode=cNode->lower;
						assert(NULL!=cNode);
					} else {
						traversingLower=false;
					}
				}
				if (traversingHigher) {
					if (higherNode!=&pSgTavl[target_sg].highest) {
						assert(NULL!=higherNode->pSeg);
						cTrack=higherNode->pSeg->track;
						if (cTrack>=track_range_bottom) {
							if (cTrack<=track_range_top) {
								// Found one in the track range.
								*pDistance=i;
								return higherNode;
							} else {
								// Exhausted the range without finding.
								traversingHigher=false;
							}
						}
						higherNode=higherNode->higher;
						assert(NULL!=higherNode);
					} else {
						traversingHigher=false;
					}
				}
			} while (traversingHigher || traversingLower);
        }

		target_sg++;
		if (target_sg>=NUMBER_OF_SG) {
			target_sg-=NUMBER_OF_SG;
		}
	}
	printf("selectTargetWithinRange(), nothing in range, startSg:%u, startTrack:%u, track_limit_bottom:%u, track_limit_top:%u.\n", startSg, startTrack, track_limit_bottom, track_limit_top);
	*pDistance=0xffff;
	return NULL;
}
#endif // (SELECTED_REORDERING==SHORTEST_DIST_WITHIN_RANGE)

#if (SELECTED_REORDERING==PATH_BUILDING_FROM_LBA)
/**
 *  @brief  To be used when attempting to push a node into the reordered list that is empty.
 *			Find a node with an LBA that is closest to && bigger than the dpReorder.lastLba from the cacheMgmt.tavl.
 *			Push the segment for the found node to dpReorder.reordered
 *			(The idea is to resume from a location that is closest to the last entry in the reordered list)
 *  @param  None
 *  @return None
 */
void pushFirstIntoReorderedList(void) {
	tavl_node_t *cNode;
	segment_t 	*tSeg;

	// Start searching the tree from dpReorder.lastLba
	cNode=searchTavl(cacheMgmt.tavl.root, dpReorder.lastLba);
	// As we checked this tree being not empty earlier, searchTavl cannot return NULL
	assert(NULL!=cNode);
	// searchTavl() returns a node that has equal or smaller LBA than dpReorder.lastLba. (it could also be cacheMgmt.tavl.lowest)
	// So start comparison from the next node.
	cNode=cNode->higher;
	assert(NULL!=cNode);
	tSeg=cNode->pSeg;
	dpReorder.lbaRangeFirst=tSeg;
	dpReorder.lbaRangeLast=tSeg;
	// Remove from LRU and push to dpReorder.reordered
	removeFromList(tSeg);
	pushToTail(tSeg, &dpReorder.reordered);
	tSeg->reordered=true;
	dpReorder.totalReordered++;
	printf("pushFirstIntoReorderedList(), tSeg:%p, tSeg->key:%u\n", tSeg, tSeg->key);
	assert(dpReorder.totalReordered==1);
	if (dpReorder.reordered.head.next!=dpReorder.reordered.tail.prev) {
		printf("dpReorder.reordered.head.next (%p) !=dpReorder.reordered.tail.prev (%p), tSeg:%p, dpReorder.totalReordered:%u\n", dpReorder.reordered.head.next, dpReorder.reordered.tail.prev, tSeg, dpReorder.totalReordered);
		assert(dpReorder.reordered.head.next==dpReorder.reordered.tail.prev);
	}

}

/**
 *  @brief  Find the next node to reorder.
 *  		As we want to feed entries in LBA ordered sequence, 
 *			we want to start from dpReorder.lbaRangeFirst and find the first node that is not reordered
 *  @param  None
 *  @return None
 */
tavl_node_t *findNextNodeToReorder(void) {
	bool		newNodeAfterLast=false;
	tavl_node_t *tNode;
	segment_t 	*tSeg;
	unsigned	nodeReviewed=0;

	// See if there is a new entry between dpReorder.lbaRangeFirst and dpReorder.lbaRangeLast
	// If there is, return the first entry found.
	tSeg=dpReorder.lbaRangeFirst;
	while (tSeg->reordered) {
		if (tSeg==dpReorder.lbaRangeLast) {
			newNodeAfterLast=true;
		}
		// Go to the next node that has a segment with higher LBA
		tNode=((tavl_node_t *)(tSeg->pNode))->higher;
		// If we have hit the ceiling, start from lowest
		if (tNode==&cacheMgmt.tavl.highest) {
			tNode=cacheMgmt.tavl.lowest.higher;
		}
		tSeg=tNode->pSeg;
		nodeReviewed++;
	}
	// If the new node is outside of LBA range (lbaRangeFirst-lbaRangeLast), update the range and last LBA
	if (newNodeAfterLast) {
		// printf("findNextNodeToReorder() reviewed %u nodes, starting between LBA:%u and %u. Picked a node with LBA:%u after the last.\n", nodeReviewed, dpReorder.lbaRangeFirst->key, dpReorder.lbaRangeLast->key, tSeg->key);
		dpReorder.lbaRangeLast=tSeg;
		dpReorder.lastLba=tSeg->key;
	}
	return tNode;
}

void reorderNewEntry(tavl_node_t *tNode) {
	segment_t 	*pCurrSeg, *pNextSeg;
	segment_t 	*pNewSeg=tNode->pSeg;
	segList_t	*reorderedList=&dpReorder.reordered;
	unsigned 	startSg, startTrack;	// Start location of the new entry, tNode
	unsigned 	endSg, endTrack;		// End location of the new entry, tNode
	unsigned	incUnorderedDist;		// Distance from the last entry in the reordered list to the tNode
	unsigned	existingDist;			// Distance of a link being evaulated
	unsigned	toNewDist;				// Distance from the lower side of a link being evaulated to the new entry, tNode
	unsigned	newToNextDist;			// Distance from the new entry, tNode to the higher side of a link being evaulated
	unsigned	minDistance;			// Minimum distance so far
	segment_t 	*pOptSubSegHead, *pOptSubSegTail;	// Pointers to the sub-segment that has minimum distance so far
	segment_t 	*pSectionStart;			// Temporary pointer to a segment
	unsigned	linkReviewed=0, subsectionLen;

	// Do not attempt to reorder already reordered entry
	assert(false==pNewSeg->reordered);
	// If there is 0 entry in the reordered list, use pushFirstIntoReorderedList(). This function needs at least one entry.
	assert(0!=dpReorder.totalReordered);

	// Remove from LRU and push to dpReorder.reordered
	removeFromList(pNewSeg);

	// If there is only one entry in the reordered list, just push to the tail.
	if (1==dpReorder.totalReordered) {
		//printf("reorderNewEntry() - first entry of LBA %u.\n", pNewSeg->key);
		pushToTail(pNewSeg, reorderedList);
		if (reorderedList->head.next==reorderedList->tail.prev) {
			// printf("reorderedList->head.next==reorderedList->tail.prev, dpReorder.totalReordered:%u\n", dpReorder.totalReordered);
			assert(reorderedList->head.next!=reorderedList->tail.prev);
		}
		return;
	}

	// TODO : Need to start from the end of the range. It is best to have a pair of SG/Track for start and end per segment.
	startSg=endSg=pNewSeg->sg;
	startTrack=endTrack=pNewSeg->track;

	// Get the distance from the last entry in the reordered list to the tNode
	getDistance(reorderedList->tail.prev->sg, reorderedList->tail.prev->track, startSg, startTrack, &incUnorderedDist);

	pOptSubSegHead=pOptSubSegTail=NULL;
	minDistance=incUnorderedDist;
	// Traverse dpReorder.reordered list and find a link where the new entry's SG fits the SGs of the two adjacent entries.
	pCurrSeg=reorderedList->head.next;
	pNextSeg=pCurrSeg->next;
	// We should have at least 2 entries in reordered list. reorderedList->head.next should point to a different segment than reorderedList->tail.prev.
	if (reorderedList->head.next==reorderedList->tail.prev) {
		// printf("reorderedList->head.next==reorderedList->tail.prev, dpReorder.totalReordered:%u\n", dpReorder.totalReordered);
		assert(reorderedList->head.next!=reorderedList->tail.prev);
	}
	while (pCurrSeg!=reorderedList->tail.prev) {
		getDistance(pCurrSeg->sg, pCurrSeg->track, pNextSeg->sg, pNextSeg->track, &existingDist);
		getDistance(pCurrSeg->sg, pCurrSeg->track, startSg, startTrack,&toNewDist);
		getDistance(endSg, endTrack, pNextSeg->sg, pNextSeg->track, &newToNextDist);
		if (existingDist==(toNewDist+newToNextDist)) {
			// Free insertion. Insert and exit.
			printf("reorderNewEntry() - Free insertion of LBA %u between %u and %u, existingDist:%d, toNewDist:%d, newToNextDist:%d, after %uth link.\n", pNewSeg->key, pCurrSeg->key, pNextSeg->key, existingDist, toNewDist, newToNextDist, linkReviewed);
			insertNextTo(pNewSeg, pCurrSeg);
			return;
		}
		// Evaluate if it is possible to get shorter distance by inserting the new entry in between and moving a range of entries around
		// But only if the distance is less than or equal to (existingDist+NUMBER_OF_SG), meaning it will cost only 1 revolution more
		// If the distance is bigger than that, that means the new entry is too far away and there is no possibility to result a lower distance
#if 1
		if ((existingDist+NUMBER_OF_SG)==(toNewDist+newToNextDist)) {
			// Evaluate only if newToNextDist is less than incUnorderedDist
			// Otherwise, new entry will be too far away from the next entry after insertion that it will increase the total distance
			if (newToNextDist<incUnorderedDist) {
				// We cannot reduce the total distance by just inserting the new entry in between two entries in the reordered list
				// but there is a still chance to reduce the total distance by,
				// 1. inserting the new entry in between two entries in the reordered list, AND
				// 2. moving a section of reordered list to the end of the reordered list
				// The section is from pSectionStart to pCurrSeg
				//
				// If we push the new entry at the tail without reordering, this would be the result
				//
				//   |------1------|----section----||-------2-----|new|
				//  head     pSectionStart      pCurrSeg        tail
				//                               pNextSeg
				//
				// If we insert the new entry in between two entries and move the section to the end, this would be the result
				//
				//   |------1------|new|------2------|----section----|
				//  head              pNextSeg   pSectionStart   pCurrSeg
				//
				// The distance of 1,2 and section are same. The difference would be,
				// From, distance(pSectionStart->prev,pSectionStart)+distance(pCurrSeg,pNextSeg)+disance(tail,new) which is incUnorderedDist
				// to, distance(pSectionStart->prev,new)+distance(new,pNextSeg)+distance(tail,pSectionStart)
				// In other words, by reordering like this, additional distance would be,
				// distance(pSectionStart->prev,new)+distance(new,pNextSeg)+distance(tail,pSectionStart)
				// -distance(pSectionStart->prev,pSectionStart)+distance(pCurrSeg,pNextSeg).
				pSectionStart=pCurrSeg;
				subsectionLen=1;
				while (pSectionStart!=reorderedList->head.next) {
					unsigned tDistPrev, tDistNext, tDistTail2Section, tDistSection, tDistCurrNext, tempDistanceSum;
					segment_t 	*pSectionStartPrev=pSectionStart->prev;
					if (pSectionStartPrev==reorderedList->head.next) {
						break;
					}

					// Get distance(pSectionStart->prev,new)
					getDistance(pSectionStartPrev->sg, pSectionStartPrev->track, pNewSeg->sg, pNewSeg->track, &tDistPrev);
					// Get distance(new,pNextSeg)
					getDistance(pNewSeg->sg, pNewSeg->track, pNextSeg->sg, pNextSeg->track, &tDistNext);
					// Get distance(tail,pSectionStart)
					getDistance(reorderedList->tail.prev->sg, reorderedList->tail.prev->track, pSectionStart->sg, pSectionStart->track, &tDistTail2Section);
					tempDistanceSum=tDistPrev+tDistNext+tDistTail2Section;
					// Get distance(pSectionStart->prev,pSectionStart)
					getDistance(pSectionStartPrev->sg, pSectionStartPrev->track, pSectionStart->sg, pSectionStart->track, &tDistSection);
					// Get distance(pCurrSeg,pNextSeg) and subtract
					getDistance(pCurrSeg->sg, pCurrSeg->track, pNextSeg->sg, pNextSeg->track, &tDistCurrNext);
					if (tempDistanceSum>=(tDistSection+tDistCurrNext)) {
						tempDistanceSum-=(tDistSection+tDistCurrNext);
						if (tempDistanceSum<minDistance) {
							printf("reorderNewEntry() LBA:%u - subsection found with incremental distance:%u, smaller than minDistance:%u, length %u.\n", pNewSeg->key, tempDistanceSum, minDistance, subsectionLen);
							printf("(pSectionStart->prev,new):%u, (new,pNextSeg):%u, (tail,pSectionStart):%u, (pSectionStart->prev,pSectionStart):%u, (pCurrSeg,pNextSeg):%u.\n", tDistPrev, tDistNext, tDistTail2Section, tDistSection, tDistCurrNext);
#if 0
							printf("pSectionStartPrev LBA %u (sg:%u,track:%u).\n", pSectionStartPrev->key, pSectionStartPrev->sg, pSectionStartPrev->track);
							printf("pSectionStart LBA %u (sg:%u,track:%u)\n", pSectionStart->key, pSectionStart->sg, pSectionStart->track);
							printf("pCurrSeg LBA %u (sg:%u,track:%u)\n", pCurrSeg->key, pCurrSeg->sg, pCurrSeg->track);
							printf("pNextSeg LBA %u (sg:%u,track:%u)\n", pNextSeg->key, pNextSeg->sg, pNextSeg->track);
							printf("Tail LBA %u (sg:%u,track:%u)\n", reorderedList->tail.prev->key, reorderedList->tail.prev->sg, reorderedList->tail.prev->track);
							printf("pNewSeg LBA %u (sg:%u,track:%u)\n", pNewSeg->key, pNewSeg->sg, pNewSeg->track);
#else
							printf("pSectionStartPrev LBA %u (sg:%u,track:%u), pSectionStart LBA %u (sg:%u,track:%u), pCurrSeg LBA %u (sg:%u,track:%u), pNextSeg LBA %u (sg:%u,track:%u), Tail LBA %u (sg:%u,track:%u), pNewSeg LBA %u (sg:%u,track:%u).\n", 
								pSectionStartPrev->key, pSectionStartPrev->sg, pSectionStartPrev->track,
								pSectionStart->key, pSectionStart->sg, pSectionStart->track,
								pCurrSeg->key, pCurrSeg->sg, pCurrSeg->track,
								pNextSeg->key, pNextSeg->sg, pNextSeg->track,
								reorderedList->tail.prev->key, reorderedList->tail.prev->sg, reorderedList->tail.prev->track,
								pNewSeg->key, pNewSeg->sg, pNewSeg->track);
#endif

							minDistance=tempDistanceSum;
							pOptSubSegHead=pSectionStart;
							pOptSubSegTail=pCurrSeg;
							// Break out of while loop on the first subsection that reduces the total distance.
							break;
						}
					}
					pSectionStart=pSectionStartPrev;
					subsectionLen++;
					// TODO : Break out of the while loop if the pSectionStartPrev gets too far away from pNewSeg. 
					// I don't know exact condition to detect yet.
					if (subsectionLen>=NUMBER_OF_REORDERED) {
						printf("reorderNewEntry() - subsectionLen(%u)>=NUMBER_OF_REORDERED(%u), pSectionStart->key:%u, pSectionStart:%p, pSectionStartPrev:%p, reorderedList->head:%p, reorderedList->tail:%p\n", subsectionLen, NUMBER_OF_REORDERED, pSectionStart->key, pSectionStart, pSectionStartPrev, &reorderedList->head, &reorderedList->tail);
						assert(subsectionLen<NUMBER_OF_REORDERED);
					}
				}
			}
		}
#endif

		pCurrSeg=pNextSeg;
		pNextSeg=pCurrSeg->next;
		linkReviewed++;
		if (linkReviewed>=NUMBER_OF_REORDERED) {
			printf("reorderNewEntry() - linkReviewed(%u)>=NUMBER_OF_REORDERED(%u), pCurrSeg:%p, pNextSeg:%p, reorderedList->head:%p, reorderedList->tail:%p\n", linkReviewed, NUMBER_OF_REORDERED, pCurrSeg, pNextSeg, &reorderedList->head, &reorderedList->tail);
			assert(linkReviewed<NUMBER_OF_REORDERED);
		}
	}

	if (minDistance<incUnorderedDist) {
		// If we have found a place to insert the new entry, insert next to pSegMinDistance.
		printf("reorderNewEntry() - Reduced total distance from incUnorderedDist:%d to minDistance::%d.\n", incUnorderedDist, minDistance);
		printf("LBA %u (sg:%u,track:%u) between %u (sg:%u,track:%u) and %u (sg:%u,track:%u).\n", pNewSeg->key, pNewSeg->sg, pNewSeg->track, pOptSubSegHead->prev->key, pOptSubSegHead->prev->sg, pOptSubSegHead->prev->sg, pOptSubSegTail->next->key, pOptSubSegTail->next->sg, pOptSubSegTail->next->track);



		// Why did I do this? insertNextTo(pNewSeg, pOptSubSegTail);




		// Move the section of the reordered list to the tail and update tail.
		// From the following,
		//   |------1------|-------section-------|--------2--------|
		//  head            L pOptSubSegHead    L pOptSubSegTail
		//
		// to the following,
		//
		//   |------1------|new|--------2--------|-------section-------|
		//  head                                  L pOptSubSegHead    L pOptSubSegTail
		//

    	pNewSeg->prev=pOptSubSegHead->prev;
    	pNewSeg->next=pOptSubSegTail->next;
		pOptSubSegHead->prev->next=pNewSeg;
		pOptSubSegTail->next->prev=pNewSeg;

		reorderedList->tail.prev->next=pOptSubSegHead;
		pOptSubSegHead->prev=reorderedList->tail.prev;
		pOptSubSegTail->next=&reorderedList->tail;
		reorderedList->tail.prev=pOptSubSegTail;
	} else {
		// If we couldn't find a place to insert the new entry, insert to the tail of reorederedList by default.
		printf("reorderNewEntry() - Adding an entry of LBA %u to the tail after %uth link.\n", pNewSeg->key, linkReviewed);
		pushToTail(pNewSeg, reorderedList);
	}
}

/**
 *  @brief  Fill reordered list
 *  @param  None
 *  @return None
 */
void fillReorderedList(void) {
	tavl_node_t *tNode;
	segment_t 	*tSeg;
	// If there is nothing in cache, return
	if (NULL==cacheMgmt.tavl.root) {
		return;
	}

	// If there is no reordered entry, find a segment to push to the empty reordered list
	if (0==dpReorder.totalReordered) {
		// printf("fillReorderedList() - calling pushFirstIntoReorderedList().\n");
		pushFirstIntoReorderedList();
	}

	tNode=findNextNodeToReorder();
	// printf("findNextNodeToReorder() returned tNode:%p, tNode->pSeg->key:%u.\n", tNode, tNode->pSeg->key);
	// Fill until there are enough number of entries in reordered list, or no more new entries, or LBA range is half of revolution away.
	while ((dpReorder.totalReordered<NUMBER_OF_REORDERED)&&(dpReorder.totalReordered<cacheMgmt.tavl.active_nodes)) {
		// Only reorder entries that have not been reordered already.
		if (!tNode->pSeg->reordered) {
			reorderNewEntry(tNode);
			tNode->pSeg->reordered=true;
			dpReorder.totalReordered++;
			// Only update the range and the last LBA if we just handled an entry that is outside of the current range
			if (tNode->pSeg->key>dpReorder.lastLba) {
				dpReorder.lbaRangeLast=tNode->pSeg;
				dpReorder.lastLba=tNode->pSeg->key;
			}
		}
		tNode=tNode->higher;
	}
}
#endif // (SELECTED_REORDERING==PATH_BUILDING_FROM_LBA)

#if (SELECTED_REORDERING==LBA_SAWTOOTH_REORDERING)
/**
 *  @brief  Search the target from the current location set in cacheMgmt and return the target
 * 			This function returns the node that is right after the current node in the LBA ordered thread
 * 			In other words, it provides LBA sawtooth reordering that sweeps from lowest to highest then wraps around.
 * 			With 10000 entries to reorder at a time & 1,000,000 loop, this scheme is about 4.54 times faster than unreordered
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
	unsigned 	distToHigher;
	tavl_node_t *higherNode;

	// Just get the higher node if it is already set. Otherwise, pick the lowest.
	if (cacheMgmt.pHigherNode) {
		if (cacheMgmt.pHigherNode==&cacheMgmt.tavl.highest) {
			cacheMgmt.pHigherNode=cacheMgmt.tavl.lowest.higher;
		}
		higherNode=cacheMgmt.pHigherNode;
		if (higherNode->pSeg==NULL) {
			assert(NULL!=higherNode->pSeg);
		}
	} else {
		cacheMgmt.pHigherNode=cacheMgmt.tavl.lowest.higher;
		higherNode=cacheMgmt.pHigherNode;
	}
	getDistance(cacheMgmt.currentSg, cacheMgmt.currentTrack, higherNode->pSeg->sg, higherNode->pSeg->track, &distToHigher);

	// Just go to the node with higher LBA.
	*pDistance=distToHigher;
	return higherNode;
}
#elif (SELECTED_REORDERING==SHORTEST_DIST)
/**
 *  @brief  Search the target from the current location set in cacheMgmt and return the target
 * 			This function returns the node that is closest from the current position
 * 			With 10000 entries to reorder at a time & 1,000,000 loop, this scheme is about 13.4 times faster than unreordered
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
	unsigned	shortestDist;
	tavl_node_t *shortestDistNode;

	// First find the shortest distance target.
	shortestDistNode=selectTarget(cacheMgmt.currentLba, cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDist);
	assert(NULL!=shortestDistNode);

	*pDistance=shortestDist;
	return shortestDistNode;
}

#elif (SELECTED_REORDERING==SHORTEST_DIST_AND_LBA)
/**
 *  @brief  Search the target from the current location set in cacheMgmt and return the target
 * 			This function returns either,
 * 			1. the node that is closest from the current position, if the distance is considerably shorter (<66.7%) than #2
 * 			2. the node that is right after the current node in the LBA ordered thread
 * 			With 10000 entries to reorder at a time & 1,000,000 loop, this scheme is about 13.4 times faster than unreordered
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
	unsigned	shortestDist;
	unsigned 	distToHigher;
	tavl_node_t *shortestDistNode, *higherNode;

	// Set the max possible distance for a case of no higher node.
	distToHigher=SEEK_TIME_LIMIT;
	// First find the shortest distance target.
	shortestDistNode=selectTarget(cacheMgmt.currentLba, cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDist);

	assert(NULL!=shortestDistNode);
	// Next, see if the node that is higher than current node is not much farther.
	if (cacheMgmt.pHigherNode) {
		if (cacheMgmt.pHigherNode==&cacheMgmt.tavl.highest) {
			cacheMgmt.pHigherNode=cacheMgmt.tavl.lowest.higher;
		}
		higherNode=cacheMgmt.pHigherNode;
		if (higherNode->pSeg==NULL) {
			assert(NULL!=higherNode->pSeg);
		}
		getDistance(cacheMgmt.currentSg, cacheMgmt.currentTrack, higherNode->pSeg->sg, higherNode->pSeg->track, &distToHigher);
	}

	if ((shortestDist+(shortestDist>>1)) < distToHigher) {
		// If 1.5 * shortest distance is still smaller than the distance to the node with higher LBA,
		// take the shortest distance node.
		*pDistance=shortestDist;
		return shortestDistNode;
	} else {
		// Otherwise, just go to the node with higher LBA.
		*pDistance=distToHigher;
		return higherNode;
	}
}
#elif (SELECTED_REORDERING==SHORTEST_DIST_WITHIN_RANGE)
/**
 *  @brief  Search the target from the current location set in cacheMgmt and return the target
 * 			This function returns the node that is closest from the current position, within a sliding range from the lowest LBA to the highest.
 * 			This function allows out of range node, if
 * 				- if it is free (can be inserted to the shortest distance node in the range without adding any cost),
 * 				- if 2 out or range nodes can be completed at less than 75% cost of shortest distance node within range,
 * 				- if we can return with small additional cost (25%),
 * 			The range is defined by (start track - cacheMgmt.maxBacktrack, start track + cacheMgmt.maxTrackRange) where,
 *				cacheMgmt.maxTrackRange : the number of tracks that can be covered in half revolution),
 *				cacheMgmt.maxBacktrack : a fixed percentage of cacheMgmt.maxTrackRange
 * 			With 10000 entries to reorder at a time & 1,000,000 loop, this scheme is faster than unreordered by the following factors.
 *          When cacheMgmt.maxBacktrack is 100% of cacheMgmt.maxTrackRange: 13.580
 *          When cacheMgmt.maxBacktrack is 7/8 of cacheMgmt.maxTrackRange: 13.687
 *          When cacheMgmt.maxBacktrack is 6/8 of cacheMgmt.maxTrackRange: 13.612
 *          When cacheMgmt.maxBacktrack is 5/8 of cacheMgmt.maxTrackRange: 13.739
 *          When cacheMgmt.maxBacktrack is 4/8 of cacheMgmt.maxTrackRange: 13.598
 *          When cacheMgmt.maxBacktrack is 3/8 of cacheMgmt.maxTrackRange: 13.512
 *          When cacheMgmt.maxBacktrack is 2/8 of cacheMgmt.maxTrackRange: 13.524
 *          When cacheMgmt.maxBacktrack is 1/8 of cacheMgmt.maxTrackRange: 13.333
 *          When cacheMgmt.maxBacktrack is 0/8 of cacheMgmt.maxTrackRange: 13.497
 *
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
	unsigned	shortestDist, returnDist, shortestDistWithinRange, secondDist;
	tavl_node_t *shortestDistNode, *returnDistNode, *shortestDistNodeWithinRange, *secondDistNode;

	// First, find the shortest distance target within the range
	shortestDistNodeWithinRange=selectTargetWithinRange(cacheMgmt.currentLba, cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDistWithinRange);
	if (NULL==shortestDistNodeWithinRange) {
		// There was none in the range. Just return shortestDistNode.
		shortestDistNode=selectTarget(cacheMgmt.currentLba, cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDist);
		printf("selectTargetFromCurrent() from startTrack:%u, nothing in range. taking LBA:%u, track:%u, shortest dist:%u\n", cacheMgmt.currentTrack, shortestDistNode->pSeg->key, shortestDistNode->pSeg->track, shortestDist);
		*pDistance=shortestDist;
		return shortestDistNode;
	}

	// Second, find the shortest distance target
	shortestDistNode=selectTarget(cacheMgmt.currentLba, cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDist);
	assert(NULL!=shortestDistNode);
	// If they are same, we are lucky. Return right away
	if (shortestDistNodeWithinRange==shortestDistNode) {
		printf("selectTargetFromCurrent() from startTrack:%u, shortest is within range. taking LBA:%u, track:%u, shortest dist:%u\n", cacheMgmt.currentTrack, shortestDistNode->pSeg->key, shortestDistNode->pSeg->track, shortestDist);
		*pDistance=shortestDist;
		return shortestDistNode;
	}

#if 0
	// If not, check if we can find 2 consecutive nodes, much cheaper than the shortest distance within range.
	secondDistNode=selectTarget(shortestDistNode->pSeg->sg, shortestDistNode->pSeg->track, &secondDist);
	assert(NULL!=secondDistNode);
	// If they are much cheaper (like, the path to those two is shorter than 75% of the shortest distance within range),
	// we will let it go out of range. Return right away
	if ((shortestDist+secondDist)<=(shortestDistWithinRange-(shortestDistWithinRange>>2))) {
		printf("selectTargetFromCurrent(), we can complete 2 out of ranges with total dist:%d, shortest dist within range:%u\n", shortestDist+secondDist, shortestDistWithinRange);
		*pDistance=shortestDist;
		return shortestDistNode;
	}
#endif

	// If not, check if we can return into the range.
	returnDistNode=selectTargetWithinRange(shortestDistNode->pSeg->key, shortestDistNode->pSeg->sg, shortestDistNode->pSeg->track, &returnDist);
	if (NULL==returnDistNode) {
		// There was none in the range. Just return shortestDistNodeWithinRange.
		printf("selectTargetFromCurrent() from startTrack:%u, shortest cannot return into range (track:%u to range starting with track:%u), taking within range LBA:%u, track:%u, dist:%u\n", cacheMgmt.currentTrack, shortestDistNode->pSeg->track, dpReorder.lbaRangeFirst->track, shortestDistNodeWithinRange->pSeg->key, shortestDistNodeWithinRange->pSeg->track, shortestDistWithinRange);
		*pDistance=shortestDistWithinRange;
		return shortestDistNodeWithinRange;
	}
	// If we can return with small additional cost (25%), we are lucky. Return right away
	// Next time, we will find this returnDistNode as the next destination
	if ((shortestDist+returnDist)<=(shortestDistWithinRange+(shortestDistWithinRange>>2))) {
		printf("selectTargetFromCurrent() from startTrack:%u, we can side trip to out of range (track:%u) and return back to LBA:%u, track:%u, total dist:%u, dist within range:%u\n", cacheMgmt.currentTrack, shortestDistNode->pSeg->track, shortestDistNodeWithinRange->pSeg->key, shortestDistNodeWithinRange->pSeg->track, shortestDist+returnDist, shortestDistWithinRange);
		*pDistance=shortestDist;
		return shortestDistNode;
	}

	// Otherwise, return the shortest distance node within range
	printf("selectTargetFromCurrent() from startTrack:%u, No side trip available. shortest dist within range:%u, shortestDist:%u, returnDist:%u, secondDist:%u\n", cacheMgmt.currentTrack, shortestDistWithinRange, shortestDist, returnDist, secondDist);
	*pDistance=shortestDistWithinRange;
	return shortestDistNodeWithinRange;
}
#elif (SELECTED_REORDERING==PATH_BUILDING_FROM_LBA)
/**
 *  @brief  Select the target from the reordered list
 * 			The reordered list should already have a non-zero number of entries. If not, put an entry and use it
 * 			With 10000 entries to reorder at a time & 1,000,000 loop, this scheme is about ? times faster than unreordered
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
	segment_t *tSeg;

#if 1
	// For the time being, fill reordered list when we select the target. 
	// TODO : filling reordered list should be done when we are not in a critical path, i.e. after completing I/O, after inserting a new I/O, etc.
	fillReorderedList();
#else
	// If there is no reordered entry, find a segment to push to the empty reordered list
	if (0==dpReorder.totalReordered) {
		pushFirstIntoReorderedList();
	}
#endif
	tSeg=dpReorder.reordered.head.next;
	assert(NULL!=tSeg);
	getDistance(cacheMgmt.currentSg, cacheMgmt.currentTrack, tSeg->sg, tSeg->track, pDistance);
	return((tavl_node_t *)(tSeg->pNode));
}
#endif

/**
 *  @brief  Search the target from the current location set in cacheMgmt and write the LBA to the given pointer.
 *  @param  unsigned *pTargetLba - pointer for the target LBA
 *			unsigned *pDistance - pointer for the distance
 *  @return None
 */
void selectTargetLba(unsigned *pTargetLba, unsigned *pDistance) {
	tavl_node_t *tNode=selectTargetFromCurrent(pDistance);
	*pTargetLba=tNode->pSeg->key;
}


/**
 *  @brief  Remove the node with the given LBA (Caller completed the operation to the target LBA)
 *			Update the current to the given target
 *			Note that the node is not given so the node needs to be searched using the target LBA.
 *  @param  unsigned targetLba - LBA of the target
 *  @return None
 */
void completeTarget(unsigned targetLba) {
	segment_t *x, *pHigherSeg;
	tavl_node_t	*currentNode;

	currentNode=searchAvl(cacheMgmt.tavl.root, targetLba);
	assert(currentNode!=NULL);
	assert(currentNode->pSeg!=NULL);

	cacheMgmt.pHigherNode=currentNode->higher;
	assert(cacheMgmt.pHigherNode!=NULL);

	if (cacheMgmt.pHigherNode==&cacheMgmt.tavl.highest) {
		cacheMgmt.pHigherNode=cacheMgmt.tavl.lowest.higher;
	}

	pHigherSeg=cacheMgmt.pHigherNode->pSeg;
	if (pHigherSeg==NULL) {
		assert(pHigherSeg!=NULL);
	}

	cacheMgmt.currentSg=currentNode->pSeg->sg;
	cacheMgmt.currentTrack=currentNode->pSeg->track;
	cacheMgmt.currentLba=targetLba;
	x=currentNode->pSeg;
	if ((NULL==x->prev) || (NULL==x->next)) {
		printf("x->prev:%p, x->next:%p, x->pNode:%p, x->pNodeSub:%p, x->key:%u, x->sg:%u, x->track:%u, x->reordered:%d\n", x->prev, x->next, x->pNode, x->pNodeSub, x->key, x->sg, x->track, x->reordered);
		assert(NULL!=x->prev);
		assert(NULL!=x->next);
	}
	freeNode(x);
	if (pHigherSeg!=cacheMgmt.pHigherNode->pSeg) {
		cacheMgmt.pHigherNode=(tavl_node_t	*)(pHigherSeg->pNode);
	}

	// Make sure the segment and both nodes are still linked.
	assert(x==((tavl_node_t *)(x->pNode))->pSeg);
	assert(x==((tavl_node_t *)(x->pNodeSub))->pSeg);

	assert(pHigherSeg==((tavl_node_t *)(pHigherSeg->pNode))->pSeg);
	assert(pHigherSeg==((tavl_node_t *)(pHigherSeg->pNodeSub))->pSeg);
}

void initCache(int maxNode) {
    unsigned 	i;
	double 		temp;

    // Initialize cache management data structure
    // 1. Initialize cacheMgmt.
    cacheMgmt.tavl.root = NULL;
    cacheMgmt.tavl.active_nodes = 0;
    initNode(&cacheMgmt.tavl.lowest);
    initNode(&cacheMgmt.tavl.highest);
    cacheMgmt.tavl.lowest.higher=&cacheMgmt.tavl.highest;
    cacheMgmt.tavl.highest.lower=&cacheMgmt.tavl.lowest;
    initSegment(&cacheMgmt.locked.head);
    initSegment(&cacheMgmt.locked.tail);
    cacheMgmt.locked.head.next=&cacheMgmt.locked.tail;
    cacheMgmt.locked.tail.prev=&cacheMgmt.locked.head;
    initSegment(&cacheMgmt.lru.head);
    initSegment(&cacheMgmt.lru.tail);
    cacheMgmt.lru.head.next=&cacheMgmt.lru.tail;
    cacheMgmt.lru.tail.prev=&cacheMgmt.lru.head;
    initSegment(&cacheMgmt.dirty.head);
    initSegment(&cacheMgmt.dirty.tail);
    cacheMgmt.dirty.head.next=&cacheMgmt.dirty.tail;
    cacheMgmt.dirty.tail.prev=&cacheMgmt.dirty.head;
    initSegment(&cacheMgmt.free.head);
    initSegment(&cacheMgmt.free.tail);
    cacheMgmt.free.head.next=&cacheMgmt.free.tail;
    cacheMgmt.free.tail.prev=&cacheMgmt.free.head;

    // 2. Initialize each segment and push into cacheMgmt.free.
	pSegmentPool=malloc(maxNode*sizeof(segment_t));
	pNodePool=malloc(2*maxNode*sizeof(tavl_node_t));
	assert(NULL!=pSegmentPool);
	assert(NULL!=pNodePool);
	// Note that one segment corresponds to 2 nodes - one pNode and one pNodeSub
    for (i = 0; i < maxNode; i++) {
        initSegment(&pSegmentPool[i]);
        initNode(&pNodePool[2*i]);
        initNode(&pNodePool[2*i+1]);
        pNodePool[2*i].pSeg=&pSegmentPool[i];
        pNodePool[2*i+1].pSeg=&pSegmentPool[i];
        pSegmentPool[i].pNode=(void *)&pNodePool[2*i];
        pSegmentPool[i].pNodeSub=(void *)&pNodePool[2*i+1];
        pushToTail(&pSegmentPool[i], &cacheMgmt.free);
    }

	// 3. Initialize all SG root as null.
	pSgTavl=malloc(NUMBER_OF_SG*sizeof(tavl_t));
	assert(NULL!=pSgTavl);
    for (i = 0; i < NUMBER_OF_SG; i++) {
        pSgTavl[i].root=NULL;
		initNode(&pSgTavl[i].lowest);
		initNode(&pSgTavl[i].highest);
		pSgTavl[i].lowest.higher=&pSgTavl[i].highest;
		pSgTavl[i].highest.lower=&pSgTavl[i].lowest;
		pSgTavl[i].active_nodes=0;
    }

	// 4. Initialize the current LBA to 0, current node to NULL and calculate current SG/track.
	cacheMgmt.currentLba=0;
	cacheMgmt.pHigherNode=NULL;
	getPhyFromLba(cacheMgmt.currentLba, &cacheMgmt.currentSg, &cacheMgmt.currentTrack);


	// 5. Allocate and initialize (a fake) inverse seek profile table
	// Allocating table size to accomodate 3x of revolution. Assuming that that can cover the worst case of full seek + 1 revolution.
	pInvSeekProfile=malloc(SEEK_TIME_LIMIT*sizeof(unsigned));
	double	offsetForSeek=(6.4*100)-((double)(100-10)*(double)(100-10)/100);
	assert(NULL!=pInvSeekProfile);
    for (i = 0; i < SEEK_TIME_LIMIT; i++) {
		int sg_diff = (i > 10)?(i-10):0;
		temp=(double)sg_diff*(double)sg_diff/100;
		if (i>=100) {
			// A seek taking longer than 100 SGs will include full velocity seek, thus making the seek profile linear.
			// The full seek, track diff=5000, requires 802 SGs, well within 3x360=1080.
			temp=6.4*(double)i-offsetForSeek;
		}
		pInvSeekProfile[i]=(unsigned)temp;
		//printf("pInvSeekProfile[%d]:%d\n", i, pInvSeekProfile[i]);
    }
	// Set maxTrackRange with the number of track that take a half revolution.
	// This is the upper limit till which reordering can include as any farther entry will take more than 1 revolution roundtrip.
	cacheMgmt.maxTrackRange=pInvSeekProfile[NUMBER_OF_SG>>1];
	cacheMgmt.maxBacktrack=(cacheMgmt.maxTrackRange>>1); // +(cacheMgmt.maxTrackRange>>3)

	// 6. Initialize DP reorder structure
    initSegment(&dpReorder.reordered.head);
    initSegment(&dpReorder.reordered.tail);
    dpReorder.reordered.head.next=&dpReorder.reordered.tail;
    dpReorder.reordered.tail.prev=&dpReorder.reordered.head;
	dpReorder.lbaRangeFirst=NULL;
	dpReorder.lbaRangeLast=NULL;
	dpReorder.totalReordered=0;
	dpReorder.totalDist=0;
	dpReorder.lastLba=0;
}
