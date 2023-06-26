// reorderLib.h

#ifndef _REORDER_H_
#define _REORDER_H_

#include <stdbool.h>

//-----------------------------------------------------------
// Macros
//-----------------------------------------------------------
#define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#define MIN(x,y) (((x) >= (y)) ? (y) : (x))

// Ridiculously simple disk format. Single head with no ZBR. No split sectors.
#define NUMBER_OF_SG 		(360)
#define NUMBER_OF_TRACKS	(5000)
#define BLOCKS_PER_SG		(10)
#define	TRACK_SKEW			(50)
#define NUMBER_OF_BLOCKS	(NUMBER_OF_SG*NUMBER_OF_TRACKS*BLOCKS_PER_SG)	// 18000000 blocks

//-----------------------------------------------------------
// Structure definitions
//-----------------------------------------------------------
typedef struct segment {
    // Previous and Next pointer used for Locked/LRU/Dirty/Free list
    struct segment  *prev;
    struct segment  *next;
    void            *pNode;
    void            *pNodeSub;
    unsigned        key;
    unsigned        numberOfBlocks;
    unsigned        sg;
    unsigned        track;
} segment_t;

typedef struct tavl_node {
    // Left and right pointer used for tree
    struct tavl_node  *left;
    struct tavl_node  *right;
    // Lower and Higher pointer used for thread list (sorted in LBA)
    struct tavl_node  *lower;
    struct tavl_node  *higher;
    segment_t       *pSeg;
    unsigned        height;
} tavl_node_t;

typedef struct segList {
    segment_t   head;
    segment_t   tail;
} segList_t;

typedef struct tavl {
    tavl_node_t *root;
    tavl_node_t lowest;
    tavl_node_t highest;
    int         active_nodes;
} tavl_t;

typedef struct cManagement {
	tavl_t		tavl;
    segList_t   locked;
    segList_t   lru;
    segList_t   dirty;
    segList_t   free;
	tavl_node_t	*pHigherNode;
	unsigned	currentSg;
	unsigned	currentTrack;
	unsigned	currentLba;
} cManagement_t;

//-----------------------------------------------------------
// Global variables
//-----------------------------------------------------------
extern	segment_t       *pSegmentPool;
extern	tavl_node_t     *pNodePool;
extern	tavl_t 			*pSgTavl;
extern	cManagement_t   cacheMgmt;

//-----------------------------------------------------------
// Functions
//-----------------------------------------------------------
/**
 *  @brief  Initializes the given segment with a clean state
 *  @param  segment_t *pSeg - the segment to be initialized
 *  @return None
 */
extern	void initSegment(segment_t *pSeg);

/**
 *  @brief  Initializes the given node with a clean state
 *  @param  tavl_node_t *pNode - the node to be initialized
 *  @return None
 */
extern	void initNode(tavl_node_t *pNode);

/**
 *  @brief  Inserts the given segment into the tail of the given list - Locked, LRU, Dirty or Free
 *  @param  segment_t *pSeg - the segment to be inserted, segList_t *pList - the destination list
 *  @return None
 */
extern	void pushToTail(segment_t *pSeg, segList_t *pList);

/**
 *  @brief  Removes the given segment from any list - Locked, LRU, Dirty or Free
 *          Note that the function does not need to know which list the segment is removed from
 *  @param  segment_t *pSeg - the segment to be removed
 *  @return None
 */
extern	void removeFromList(segment_t *pSeg);

/**
 *  @brief  Pops a segment from the head of the given list - Locked, LRU, Dirty or Free
 *  @param  segList_t *pList - the list
 *  @return The segment that got just popped
 */
extern	segment_t *popFromHead(segList_t *pList);

/**
 *  @brief  Removes the given node from the LBA ordered thread.
 *  @param  tavl_node_t *pNode - the node to be removed
 *  @return None
 */
extern	void removeFromThread(tavl_node_t *pNode);

/**
 *  @brief  Inserts the given node before the target.
 *  @param  tavl_node_t *pNode - the node to be inserted, tavl_node_t *pTarget - target node
 *  @return None
 */
extern	void insertBefore(tavl_node_t *pNode, tavl_node_t *pTarget);

/**
 *  @brief  Inserts the given node after the target.
 *  @param  tavl_node_t *pNode - the node to be inserted, tavl_node_t *pTarget - target node
 *  @return None
 */
extern	void insertAfter(tavl_node_t *pNode, tavl_node_t *pTarget);

/**
 *  @brief  Returns the heigh of the given node
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *  @return unsigned height of the node
 */
extern	unsigned avlHeight(tavl_node_t *head);

/**
 *  @brief  Rotates the sub-tree to right (clockwise)
 *  @param  tavl_node_t *head - a node in the AVL tree - cannot be NULL
 *  @return root of the rotated sub-tree
 */
extern	tavl_node_t *rightRotation(tavl_node_t *head);

/**
 *  @brief  Rotates the sub-tree to left (counter clockwise)
 *  @param  tavl_node_t *head - a node in the AVL tree - cannot be NULL
 *  @return root of the rotated sub-tree
 */
extern	tavl_node_t *leftRotation(tavl_node_t *head);

/**
 *  @brief  Inserts the given node into the given AVL tree
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *          tavl_node_t *x - a node to be inserted
 *  @return root of the new tree
 */
extern	tavl_node_t *insertNode(tavl_node_t *head, tavl_node_t *x);

/**
 *  @brief  Removes the given segment from the given AVL tree
 *          Note that the entity being removed is segment, not a node.
 *          This is because remove operation may swap the content of the node
 *          to be removed with another node that has a key just higher.
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *          segment_t *x - a segment to be removed
 *  @return root of the new tree
 *	@note	removeNodeSub() is for removing the segment from SG tree
 */
extern	tavl_node_t *removeNode(tavl_node_t *head, segment_t *x);
extern	tavl_node_t *removeNodeSub(tavl_node_t *head, segment_t *x);

/**
 *  @brief  Searches the given AVL tree for the given key
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *          unsigned key - a key to be searched
 *  @return The node that contains the key, or NULL
 */
extern	tavl_node_t *searchAvl(tavl_node_t *head, unsigned key);

/**
 *  @brief  Searches the given TAVL tree for the given LBA
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *          unsigned lba - an LBA to be searched
 *  @return The node that contains a key that is equal or smaller than the given LBA, or NULL
 */
extern	tavl_node_t *searchTavl(tavl_node_t *head, unsigned lba);

/**
 *  @brief  Inserts the given node into the given TAVL tree.
 *          In other words,
 *          1. inserts the given node into AVL tree
 *          2. inserts the given node into the Thread
 *  @param  tavl_t *pTavl - pointer to the tavl structure
 *          tavl_node_t *x - pointer to the node to be inserted
 *  @return New root of the tree
 */
extern tavl_node_t *insertToTavl(tavl_t *pTavl, tavl_node_t *x);

// Remove a node from AVL tree, thread and list the push to free list.
// Specified list can be Locked/LRU/Dirty.
// Returns the new root.
/**
 *  @brief  Remove a segment_t from cache management TAVL tree and list & from SG TAVL tree, then push to the free list.
 *  @param  segment_t *x - segment to be removed
 *  @return None
 */
extern	void freeNode(segment_t *x);

/**
 *  @brief  Searches the given TAVL tree for the given LBA and dump the path
 *  @param  tavl_node_t *head - a node in the AVL tree, or NULL
 *          unsigned lba - an LBA to be searched
 *  @return The node that contains a key that is equal or smaller than the given LBA, or NULL
 */
extern	tavl_node_t *dumpPathToKey(tavl_node_t *head, unsigned lba);

/**
 *  @brief  Dumps all nodes in one SG.
 *  @param  unsigned sg - SG
 *  @return None
 */
extern void dumpOneSgNodes(unsigned sg);

/**
 *  @brief  Dumps all nodes in all SG.
 *  @param  None
 *  @return None
 */
extern void dumpSgNodes(void);

//-----------------------------------------------------------
// Public Functions, used by python lib
//-----------------------------------------------------------
/**
 *  @brief  Get the number of blocks (disk size)
 *  @param  unsigned *pNumberOfBlocks - pointer for number of blocks
 *  @return None
 *	@todo	Python lib will limit the LBA to NUMBER_OF_BLOCKS
 */
extern	void getNumOfBlocks(unsigned *pNumberOfBlocks);

/**
 *  @brief  Get the number of SGs
 *  @param  unsigned *pNumberOfSgs - pointer for number of SGs
 *  @return None
 */
extern	void getNumOfSgs(unsigned *pNumberOfSgs);

/**
 *  @brief  Get the number of tracks
 *  @param  getNumOfTracks(unsigned *pNumberOfTracks) - pointer for number of tracks
 *  @return None
 */
extern	void getNumOfTracks(unsigned *pNumberOfTracks);

/**
 *  @brief  Converts the given LBA to SG and track
 *  @param  unsigned lba - LBA, unsigned *pSg - pointer for SG, unsigned *pTrack - pointer for track
 *  @return None
 *	@todo	Python lib will limit the LBA to NUMBER_OF_BLOCKS
 */
extern	void getPhyFromLba(unsigned lba, unsigned *pSg, unsigned *pTrack);

/**
 *  @brief  Add an entry with the given LBA into the master TAVL tree (cacheMgmt.tavl.root) and SG TAVL tree (pSgTavl[sg].root).
 *  @param  unsigned lba : LBA (Python application will always send an LBA that does not overlap) 
 *			unsigned num_of_blocks : Number of blocks (Python lib will always set this to 1)
 *  @return None
 */
extern	void addLba(unsigned lba, unsigned num_of_blocks);

/**
 *  @brief  Search the target from the current location set in cacheMgmt.
 *			Return the target.
 *  @param  unsigned *pDistance - pointer for the distance
 *  @return the target node
 */
extern tavl_node_t *selectTargetFromCurrent(unsigned *pDistance);

/**
 *  @brief  Search the target from the current location set in cacheMgmt and write the LBA to the given pointer.
 *  @param  unsigned *pTargetLba - pointer for the target LBA
 *			unsigned *pDistance - pointer for the distance
 *  @return None
 */
extern void selectTargetLba(unsigned *pTargetLba, unsigned *pDistance);

/**
 *  @brief  Remove the node with the given LBA (Caller completed the operation to the target LBA)
 *			Update the current to the given target
 *			Note that the node is not given so the node needs to be searched using the target LBA.
 *  @param  unsigned targetLba - LBA of the target
 *  @return None
 */
extern	void completeTarget(unsigned targetLba);


/**
 *  @brief  Initializes the whole cache management structure - cacheMgmt, 
 *  @param  int maxNode - number of nodes
 *  @return None
 */
extern	void initCache(int maxNode);

extern	void tavlSanityCheck(tavl_t *pTavl);
extern	void tavlSanityCheckSub(tavl_t *pTavl);
extern  bool tavlHeightCheck(tavl_node_t *head);

#endif // _REORDER_H_
