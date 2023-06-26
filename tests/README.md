# Tests

## Test sequence
- Initialize cache with NUM_OF_TEST_NODES(default value of 10000) nodes & get the number of blocks in the device
- Insert all nodes with random LBA and number of block of 1
- Check the TAVL tree to verify all nodes are ordered by LBA
- Starting from the location of LBA 0, loop while searching the next target, removing the target and adding a new entry with a random LBA, TEST_LOOP(default 1 million) times
- Remove all nodes
- Check if LRU is empty
- Complete test by reporting the total time-distance

## How to run
- make
- ./test in Linux or test.exe in Windows