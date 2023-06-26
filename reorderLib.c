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

/**
 *  @brief  Search the target from the given SG and track.
 *			Return the LBA of the target & the distance (in number of SGs).
 *  @param  unsigned startSg - starting SG, unsigned startTrack- starting track, 
 *			unsigned *pDistance - pointer for the distance
 *  @return pointer of the node
 */
tavl_node_t *selectTarget(unsigned startSg, unsigned startTrack, unsigned *pDistance) {
    unsigned 	i;
	unsigned 	target_sg, track_diff, track_range_top, track_range_bottom;
	unsigned	cTrack;
	tavl_node_t *cNode,*higherNode, *nextNode;

	target_sg=startSg;
	for (i=0; i<3*NUMBER_OF_SG; i++) {
		// i is for indexing pInvSeekProfile[]
		// target_sg for indexing pSgTavl[]
		if (NULL!=pSgTavl[target_sg].root) {
			// If this SG has nodes, search the tree
			track_diff=pInvSeekProfile[i];
			track_range_top=startTrack+track_diff;
			track_range_top=MIN(track_range_top, NUMBER_OF_TRACKS-1);
			track_range_bottom=(startTrack>=track_diff)?startTrack-track_diff:0;

			// Start searching the tree from track_range_bottom
			cNode=searchTavl(pSgTavl[target_sg].root, track_range_bottom);
			// As we checked this tree being not empty earlier, searchTavl cannot return NULL
			assert(NULL!=cNode);
			// searchTavl() returns a node that has equal or smaller LBA than track_range_bottom. (it could also be pSgTavl[target_sg].lowest)
			// So start comparison from the next node.
			cNode=cNode->higher;
			assert(NULL!=cNode);
			while (cNode!=&pSgTavl[target_sg].highest) {
				assert(NULL!=cNode->pSeg);
				cTrack=cNode->pSeg->track;
				if (cTrack>=track_range_bottom) {
					if (cTrack<=track_range_top) {
						// Found one in the track range.
						*pDistance=i;
						return cNode;
					} else {
						// Exhausted the range without finding.
						// Break out the while loop to go to the next SG.
						break;
					}
				}
				cNode=cNode->higher;
				assert(NULL!=cNode);
			}
        }

		target_sg++;
		if (target_sg>=NUMBER_OF_SG) {
			target_sg-=NUMBER_OF_SG;
		}
	}
	*pDistance=0xffff;
	return NULL;
}

/**
 *  @brief  Search the target from the current location set in cacheMgmt.
 *			Return the target.
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
tavl_node_t *selectTargetFromCurrent(unsigned *pDistance) {
    unsigned 	i;
	unsigned 	target_sg, track_range_top, track_range_bottom;
	unsigned	shortestDist;
	unsigned 	distToHigher, tracksToHigher;
	tavl_node_t *shortestDistNode, *higherNode;

	// Set the max possible distance for a case of no higher node.
	distToHigher=3*NUMBER_OF_SG;
	// First find the shortest distance target.
	shortestDistNode=selectTarget(cacheMgmt.currentSg, cacheMgmt.currentTrack, &shortestDist);

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
		if (higherNode->pSeg->sg>=cacheMgmt.currentSg) {
			distToHigher=higherNode->pSeg->sg-cacheMgmt.currentSg;
		} else {
			distToHigher=NUMBER_OF_SG+(higherNode->pSeg->sg)-cacheMgmt.currentSg;
		}
		tracksToHigher=abs(higherNode->pSeg->track-cacheMgmt.currentTrack);
		while (tracksToHigher > pInvSeekProfile[distToHigher]) {
			distToHigher+=NUMBER_OF_SG;
		}
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
	pInvSeekProfile=malloc(3*NUMBER_OF_SG*sizeof(unsigned));
	assert(NULL!=pInvSeekProfile);
    for (i = 0; i < 3*NUMBER_OF_SG; i++) {
		int sg_diff = (i > 10)?(i-10):0;
		temp=(double)sg_diff*(double)sg_diff/40;
		if (i>=100) {
			// At sg 100, temp and the following formular need to result 202.5.
			// This results the worst case of full seek + 1 rev as 579.75 + 360 = 939.75, well within 3x360=1080.
			temp=10*(double)i-797.5;
		}
		pInvSeekProfile[i]=(unsigned)temp;
		//printf("pInvSeekProfile[%d]:%d\n", i, pInvSeekProfile[i]);
    }
}
