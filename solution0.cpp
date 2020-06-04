//////////////////////////////////////////////////////////////////////////////
// INCLUDE FILES & NAMESPACES //
//////////////////////////////////////////////////////////////////////////////
//Input / Output
#include <iostream>

//Threading
#include <thread>
#include <pthread.h>

//Utilities
#include <semaphore.h>

//Containers
#include <list>

//Types
#include <memory> //unique_ptr
#include <string>

using namespace std;
using Block = int;
using ShBlockPtr = shared_ptr<Block>;

//////////////////////////////////////////////////////////////////////////////
// CLASS DEFINITION //
//////////////////////////////////////////////////////////////////////////////
class BlockLst : public list<ShBlockPtr>{
public:
  //CONSTRUCTORS
  BlockLst() = default;

  //reserve constructor
  BlockLst(int size){
    for(int i = 0; i < size; ++i) this->emplace_back(new Block);
  }//end reserve constructor
  
  //MUTATORS 
  void link(ShBlockPtr blk){
    this->push_back(blk);
  }//end link

  ShBlockPtr unlink(){;
    ShBlockPtr rtnPtr;
    iterator blkIt;

    //get last block on list
    blkIt = (--(this->end()));
    rtnPtr = *blkIt;

    //remove that block from current list
    this->erase(blkIt);

    return rtnPtr;
  }//end unlink
};//END class BlockLst
//////////////////////////////////////////////////////////////////////////////
// END CLASS DEFINITION //
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// GLOBALS //
//////////////////////////////////////////////////////////////////////////////
unsigned short const NUM_BLOCKS = 1000;//N
sem_t coutMtx;

//////////////////////////////////////////////////////////////////////////////
// FUNCTION DECLARATIONS //
//////////////////////////////////////////////////////////////////////////////
// SYNCRONIZATION //
//----------------------------------------------------------------------------
//These functions call the associated POSIX semaphore functions
//  and do error checking/reporting on the return value
void wait(sem_t *semPtr, string const &name);
void post(sem_t *semPtr, string const &name);

//----------------------------------------------------------------------------
//Thread safe cout <<
//DEPENDS ON GLOBAL sem_t::coutMtx
//Does NOT guarantee that msgs are printed in chronological order
//  (i.e. NOT a guaranteed total order)
//DOES guarantee that only 1 msg prnts at a time
void syncCout(int id, string const& msg);

// THREAD TASKS //
//----------------------------------------------------------------------------
//places a value in the block
void produce_information_in_block(ShBlockPtr blk);

//----------------------------------------------------------------------------
//copies data in x to y
void use_block_x_to_produce_info_in_y(ShBlockPtr x, ShBlockPtr y);

//----------------------------------------------------------------------------
//chngs value of block to -1
void consume_information_in_block(ShBlockPtr blk);

//////////////////////////////////////////////////////////////////////////////
// MAIN FUNCTION //
//////////////////////////////////////////////////////////////////////////////
int main(){
  // SYNCHRONIZATION //
  
  sem_t availBlocks, freeBlocks, lst1Blocks, lst2Blocks; //block trackers
  sem_t freeMtx, lst1Mtx, lst2Mtx; //concurrent list access protection
  if(
    sem_init(&coutMtx, 0, 1) //cout strts avail
    || sem_init(&freeMtx, 0, 1) //each lst strts avail
    || sem_init(&lst1Mtx, 0, 1) 
    || sem_init(&lst2Mtx, 0, 1)
    //availBlocks guaranteed to eventually be free
    || sem_init(&availBlocks, 0, NUM_BLOCKS)
    || sem_init(&freeBlocks, 0, NUM_BLOCKS) //num blocks on each list
    || sem_init(&lst1Blocks, 0, 0)
    || sem_init(&lst2Blocks, 0, 0)
  ){ cerr << "Error initializing semaphores" << endl; }
  
  BlockLst freeLst(NUM_BLOCKS);
  BlockLst lst1, lst2;

  thread thread1(
    [&]() {
      ShBlockPtr b;
      while(true){
        //Make sure there are at least 2 availBlocks
        wait(&availBlocks, "availBlocks");
        wait(&availBlocks, "availBlocks");
        post(&availBlocks, "availBlocks");
        //we only ACTUALLY need one, so put the other back

        //wait for an availBlock to *actually* become free
        wait(&freeBlocks, "freeBlocks");
        wait(&freeMtx, "freeMtx");
          syncCout(1, "Thread 1 unlinking from freeLst");
          b = freeLst.unlink();
        post(&freeMtx, "freeMtx");

        produce_information_in_block(b);

        wait(&lst1Mtx, "lst1Mtx");
          lst1.link(b);
          syncCout(1, "Thread finished linking on to lst1");
        post(&lst1Mtx, "lst1Mtx");
        post(&lst1Blocks, "lst1Blocks");
      }//END WHILE
    }  //end thread1 lambda
  );   //end thread1

  thread thread2(
    [&]() {
      ShBlockPtr x, y;
      while(true){
        wait(&lst1Blocks, "lst1Blocks");
        wait(&lst1Mtx, "lst1Mtx");
          syncCout(2, "Thread 2 unlinking from lst1");
          x = lst1.unlink();
        post(&lst1Mtx, "lst1Mtx");

        wait(&freeBlocks, "freeBlocks");
        post(&availBlocks, "availBlocks");
        wait(&freeMtx, "freeMtx");
          syncCout(2, "Thread 2 unlinking from freeLst");
          y = freeLst.unlink();
        post(&freeMtx, "freeMtx");
        

        use_block_x_to_produce_info_in_y(x, y);

        wait(&freeMtx, "freeMtx");
          freeLst.link(x);
          syncCout(2, "Thread 2 finished linking on to freeLst");
        post(&freeMtx, "freeMtx");
        post(&freeBlocks, "freeBlocks");

        wait(&lst2Mtx, "lst2Mtx");
          lst2.link(y);
          syncCout(2, "Thread 2 finished linking on to lst2");
        post(&lst2Mtx, "lst2Mtx");
        post(&lst2Blocks, "lst2Blocks");
      } //END WHILE
    }   //end thread2 lambda
  );    //end thread2

  thread thread3(
    [&]() {
      ShBlockPtr c;
      while(true){  
        wait(&lst2Blocks, "lst2Blocks");
        wait(&lst2Mtx, "lst2Mtx");
          syncCout(3, "Thread 3 unlinking from lst2");
          c = lst2.unlink();
        post(&lst2Mtx, "lst2Mtx");
        
        consume_information_in_block(c); 

        wait(&freeMtx, "freeMtx");
          freeLst.link(c);
          syncCout(3, "Thread 3 finished linking on to freeLst");
        post(&freeMtx, "freeMtx");
        post(&freeBlocks, "freeBlocks");
      }//END WHILE
    }  //end thread3 lambda
  );   //end thread3

  thread1.join();
  thread2.join();
  thread3.join();
  
  return 0;
}//end main
//////////////////////////////////////////////////////////////////////////////
// END MAIN FUNCTION //
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// FUNCTION DEFINITIONS //
//////////////////////////////////////////////////////////////////////////////
void wait(sem_t *semPtr, string const &name){
  if (sem_wait(semPtr))
    cerr << "Error waiting "+name << endl;
}//end wait

//////////////////////////////////////////////////////////////////////////////
void post(sem_t *semPtr, string const &name){
  if(sem_post(semPtr))
    cerr << "Error posting "+name << endl;
}//end post

//////////////////////////////////////////////////////////////////////////////
//Must call stream::flush() to prevent buffering
//  from thwarting our attempts to keep messages separate
void syncCout(int id, string const& msg){
  if(sem_wait(&coutMtx))
    cerr << "Error waiting coutMtx" << endl;

  cout << msg << endl;
  cout.flush();

  if(sem_post(&coutMtx))
    cerr << "Error posting coutMtx" << endl;
}//end syncCout

//////////////////////////////////////////////////////////////////////////////
void produce_information_in_block(ShBlockPtr blk){ *blk = 1; }

//////////////////////////////////////////////////////////////////////////////
void use_block_x_to_produce_info_in_y(ShBlockPtr x, ShBlockPtr y){ *y = *x; }

//////////////////////////////////////////////////////////////////////////////
void consume_information_in_block(ShBlockPtr blk){ *blk = -1; }