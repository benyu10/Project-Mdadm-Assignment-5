#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "mdadm.h"
#include "jbod.h"
#include "net.h"


uint32_t encode_operation(jbod_cmd_t cmd, int disk_num, int block_num)
{

  uint32_t op = cmd << 26 | disk_num << 22 | block_num;
  return op;
}

void translate_address(uint32_t linear_address,
		       int *disk_num,
		       int *block_num,
		       int *offset)
{
  int block_remainder=0;
  *disk_num = linear_address / JBOD_DISK_SIZE;
  block_remainder = linear_address % JBOD_DISK_SIZE;
  *block_num = block_remainder / JBOD_BLOCK_SIZE;
  *offset = block_remainder % JBOD_BLOCK_SIZE;  
}


int seek(int disk_num, int block_num)
{
  uint32_t op;
  uint32_t ops;  
  op = encode_operation(JBOD_SEEK_TO_DISK, disk_num, 0);
  ops = encode_operation(JBOD_SEEK_TO_BLOCK, 0, block_num);
  jbod_client_operation(op, NULL);
  jbod_client_operation(ops, NULL);
  return 1;
}
int findmin(int n, int n2)
{
  if (n>n2)
    {
      return n2;
    }
  else if (n==n2)
    {
      return n;
    }
  else
    {
      return n;
    }
}

int mounted = 0; 
int mdadm_mount(void) {
   uint32_t op = encode_operation(JBOD_MOUNT, 0, 0); // pass zero's because its default
  int value = jbod_client_operation(op, NULL);
  if (value == 0)
    {
       mounted = 1;
      return 1;
    }
  return -1;
}

int mdadm_unmount(void) {
  uint32_t last_op = encode_operation(JBOD_UNMOUNT,0, 0); //last command called so its on last disk, last block
  int value = jbod_client_operation(last_op, NULL);
  if (value == 0)
    {
      mounted =0;
      return 1;
    }
  return -1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  int disk_num, block_num, offset;
  if (mounted == 0 || len > 1024 || (len && buf == NULL) || addr+ len >= (JBOD_NUM_DISKS * JBOD_DISK_SIZE)) //test cases
  {
    return -1; 
  }
 
 
  int total = len;
  int data_read = 0;
  int curr_addy = addr;
  int sumDR = 0;
  int counter = 0;
  uint8_t buf1[256];
  while (curr_addy<addr+len){
    
    translate_address(curr_addy, &disk_num, &block_num, &offset);
    seek(disk_num, block_num);
  
    if (cache_enabled()==true)
        {

          if ( cache_lookup(disk_num, block_num, buf1) == 1)
	    {
	      memcpy(buf,buf1,256-offset);
	      return 1;
	    }
	} 

    uint32_t op = encode_operation(JBOD_READ_BLOCK, 0, 0);
    jbod_client_operation(op,buf1);
    cache_insert(disk_num, block_num, buf1);
    
   if (counter == 0) //first block bc current block is equal to first block num   
    {
      memcpy(buf+sumDR, buf1+offset, findmin((JBOD_BLOCK_SIZE - offset),len));
      data_read = findmin((JBOD_BLOCK_SIZE - offset),len);
      counter += 1; //increments the counter so not in first block anymore
      //if last block, then disk + 1 to go to next disk  	
    }
   else if (total < JBOD_BLOCK_SIZE)//last block, read whats left of the data by using x
    {
      memcpy(buf+sumDR, buf1, total);
      data_read = total;
    }
  else
    {
      memcpy(buf+sumDR, buf1, JBOD_BLOCK_SIZE); //middle blocks, so read full block size. offset is zero, new block
      data_read = JBOD_BLOCK_SIZE; 
    }
   
     sumDR = sumDR + data_read;
     total = total - data_read;//keeps track of the bytes left to read
     curr_addy = curr_addy + data_read;
  }
  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if (mounted == 0 || len > 1024 || (len && buf == NULL) || (addr+len) > (JBOD_NUM_DISKS * JBOD_DISK_SIZE)) //test case
  {
    return -1;
  }
  int disk_num, block_num, offset;
 
  int total = len;
  int data_read = 0;
  int curr_addy = addr;
  int sumDR = 0;
  int counter = 0;
  uint8_t buf1[256];

  while (curr_addy<addr+len){ //keeps the loop going until the address matches

    translate_address(curr_addy, &disk_num, &block_num, &offset); //finds the disk block and offset num
    seek(disk_num, block_num); //looks for right disk and block num
    uint32_t op = encode_operation(JBOD_READ_BLOCK, 0, 0); //reads the block
    jbod_client_operation(op,buf1); //sets buf to contents of what was read
    
   if (counter == 0) //first block bc current block is equal to first block num                                                                      
    {
      memcpy(buf1+offset,buf+sumDR ,findmin((JBOD_BLOCK_SIZE - offset),len)); //writes the user data len into buf1 from buf
      seek(disk_num, block_num); 
      uint32_t ops = encode_operation(JBOD_WRITE_BLOCK, 0, 0); //writes the contents into disk
      jbod_client_operation(ops,buf1);
      data_read = findmin((JBOD_BLOCK_SIZE - offset),len); //calculates what was written
     
      counter += 1; //increments the counter so not in first block anymore                                                                           
    }
   else if (total < JBOD_BLOCK_SIZE)//last block, read whats left of the data by using x                                                              
    {
      
      memcpy(buf1, buf+sumDR, total);
      seek(disk_num, block_num);
      uint32_t ops = encode_operation(JBOD_WRITE_BLOCK, 0, 0);
      jbod_client_operation(ops,buf1);

      data_read = total;
    }
  else
    {
      memcpy(buf1, buf+sumDR, JBOD_BLOCK_SIZE); //middle blocks, so read full block size. offset is zero, new block                                   
      seek(disk_num, block_num);
      uint32_t ops = encode_operation(JBOD_WRITE_BLOCK, 0, 0);
      jbod_client_operation(ops,buf1);
  
      data_read = JBOD_BLOCK_SIZE;
    }

   sumDR = sumDR + data_read; //the sum of all data read by adding it together
     total = total - data_read;//keeps track of the bytes left to read by subtracting total by data read                                                                                
     curr_addy = curr_addy + data_read; //calculates the address by adding data already read
  }

 return len;
 }
