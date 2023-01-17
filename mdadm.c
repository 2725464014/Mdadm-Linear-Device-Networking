#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "mdadm.h"
#include "jbod.h"
uint32_t op(uint32_t DiskID, uint32_t BlockID, uint32_t Command, uint32_t Reserved)
{
	uint32_t diskId = DiskID << 8;						 // field of disk
	uint32_t blockId = BlockID;							 // field of block
	uint32_t command = Command << 12;					 // field of command
	uint32_t reserved = Reserved << 18;					 // field of reserved
	uint32_t op = reserved | command | blockId | diskId; // union all
	return op;
}
int ismounted = 0;
int is_written = 0;

int mdadm_mount(void)
{

	if (ismounted == 1) // if ismounted return -1
		return -1;
	if (jbod_client_operation(op(0, 0, JBOD_MOUNT, 0), NULL) == 0) // if mount succeed ismounted=1 return 1
	{
		ismounted = 1;
		return 1;
	}
	else
		return -1;
}

int mdadm_unmount(void)
{
	if (ismounted == 0) // if not ismounted return -1
		return -1;
	if (jbod_client_operation(op(0, 0, JBOD_UNMOUNT, 0), NULL) == 0) // if unmount succeed ismounted=0 return 1
	{

		ismounted = 0;
		return 1;
	}
	else
		return -1;
}

int mdadm_write_permission(void)
{

	if (is_written == 1) // if ismounted return -1
		return -1;
	if (jbod_client_operation(op(0, 0, JBOD_WRITE_PERMISSION, 0), NULL) == 0) // if mount succeed ismounted=1 return 1
	{
		is_written = 1;
		return 1;
	}
	else
		return -1;
}

int mdadm_revoke_write_permission(void)
{
	if (is_written == 0) // if not ismounted return -1
		return -1;
	if (jbod_client_operation(op(0, 0, JBOD_REVOKE_WRITE_PERMISSION, 0), NULL) == 0) // if unmount succeed ismounted=0 return 1
	{

		is_written = 0;
		return 1;
	}
	else
		return -1;
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)
{
	if (ismounted == 0) // check if argument are in restrirctions
		return -1;
	if (start_addr + read_len > 1048576)
		return -1;
	if (read_len >= 2048 || read_len < 0)
		return -1;
	if (read_buf == NULL && read_len != 0)
		return -1;
	if (start_addr == 0 && read_buf == NULL && read_len == 0)
		return 0;

	uint8_t *tempbuf = (uint8_t *)malloc(256);	   // set temper buffer
	uint32_t curAddr = start_addr;				   // set temper address
	uint32_t usedlen = 0;						   // set function used length
	uint32_t byteRemain = 0;					   // set byte remain in the block
	uint32_t startDisk = curAddr / 65536;		   // set disk number we use in
	uint32_t startBlock = (curAddr % 65536) / 256; // set block number we are in
	uint32_t startByte = (curAddr % 65536) % 256;  // set byte position we are in

	while (usedlen < read_len)
	{
		startDisk = curAddr / 65536;		  // update disk
		startBlock = (curAddr % 65536) / 256; // update block
		startByte = (curAddr % 65536) % 256;
		jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
		jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);

		byteRemain = 256 - startByte; // how many byte left in block
		if (!cache_enabled() || cache_lookup(startDisk, startBlock, tempbuf) == -1)
		{
			jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);
			cache_insert(startDisk, startBlock, tempbuf);
		}
		// jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf); // read the block into temper buffer
		if (byteRemain > read_len - usedlen) // can read in one block
		{
			memcpy(read_buf + usedlen, tempbuf + startByte, read_len - usedlen); // copy from temper to readbuf
			curAddr += read_len - usedlen;										 // address we are in
			usedlen += read_len - usedlen;										 // add bytes to used length
		}
		else // cant read in one block
		{
			jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
			jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);

			memcpy(read_buf + usedlen, tempbuf + startByte, byteRemain); // copy only byte left in block to readbuf
			usedlen += byteRemain;
			curAddr += byteRemain;
			if (!cache_enabled() || cache_lookup(startDisk, startBlock, tempbuf) == -1)
			{
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);
				cache_insert(startDisk, startBlock, tempbuf);
			}
			if (read_len - usedlen > 256) // more than one block
			{
				startDisk = curAddr / 65536;		  // update disk
				startBlock = (curAddr % 65536) / 256; // update block
				startByte = (curAddr % 65536) % 256;
				jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
				jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf); // read the block
				memcpy(read_buf + usedlen, tempbuf, 256);					  // copy all
				usedlen += 256;
				curAddr += 256;
			}
		}
		// printf("read:%d %d ", curAddr, usedlen);
	}
	free(tempbuf);
	tempbuf = NULL;
	return usedlen;
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)
{
	if (is_written == 0) // check if argument are in restrirctions
		return -1;
	if (start_addr + write_len > 1048576)
		return -1;
	if (write_len >= 2048 || write_len < 0)
		return -1;
	if (write_buf == NULL && write_len != 0)
		return -1;
	if (start_addr == 0 && write_buf == NULL && write_len == 0)
		return 0;
	uint8_t *tempbuf = (uint8_t *)malloc(256);	   // set temper buffer
	uint32_t curAddr = start_addr;				   // set temper address
	uint32_t usedlen = 0;						   // set function used length
	uint32_t byteRemain = 0;					   // set byte remain in the block
	uint32_t startDisk = curAddr / 65536;		   // set disk number we use in
	uint32_t startBlock = (curAddr % 65536) / 256; // set block number we are in
	uint32_t startByte = (curAddr % 65536) % 256;  // set byte position we are in

	while (usedlen < write_len)
	{

		startDisk = curAddr / 65536;		  // update disk
		startBlock = (curAddr % 65536) / 256; // update block
		startByte = (curAddr % 65536) % 256;
		jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
		jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
		byteRemain = 256 - startByte; // how many byte left in block
		int cache_updated = 0;
		if (cache_enabled())
		{
			if (cache_lookup(startDisk, startBlock, tempbuf) != 1)
			{
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);		 // read the block
				jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
				jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
			}
			else
			{
				cache_updated = 1;
			}
		}
		else
		{
			jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);		 // read the block
			jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
			jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
		}

		if (byteRemain > write_len - usedlen) // can write in one block
		{
			jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf); // read the block
			/*if (!cache_enabled() || cache_lookup(startDisk, startBlock, tempbuf) == -1)
			{
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);
				cache_insert(startDisk, startBlock, tempbuf);
			}*/
			memcpy(tempbuf + startByte, write_buf + usedlen, write_len - usedlen); // copy from readbuf to tembuf
			jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL);   // reseek
			jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
			curAddr += write_len - usedlen;
			usedlen += write_len - usedlen;								   // add bytes to used length
			jbod_client_operation(op(0, 0, JBOD_WRITE_BLOCK, 0), tempbuf); // write the temper buffer into block
																		   // cache_insert(startDisk, startBlock, tempbuf);
		}
		else // cant write in one block
		{
			jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf); // read the block
			/*if (!cache_enabled() || cache_lookup(startDisk, startBlock, tempbuf) == -1)
			{
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);
				cache_insert(startDisk, startBlock, tempbuf);
			}*/
			memcpy(tempbuf + startByte, write_buf + usedlen, byteRemain);		 // copy only byte left in block to writebuf
			jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
			jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
			usedlen += byteRemain;
			curAddr += byteRemain;										   // address we are in
			jbod_client_operation(op(0, 0, JBOD_WRITE_BLOCK, 0), tempbuf); // write the temper buffer into block
			// cache_insert(startDisk, startBlock, tempbuf);

			while (write_len - usedlen > 256) // more than one block
			{
				startDisk = curAddr / 65536;		  // update disk
				startBlock = (curAddr % 65536) / 256; // update block
				startByte = (curAddr % 65536) % 256;
				jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
				jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
				jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf); // read the block
				/*if (!cache_enabled() || cache_lookup(startDisk, startBlock, tempbuf) == -1)
				{
					jbod_client_operation(op(0, 0, JBOD_READ_BLOCK, 0), tempbuf);
					cache_insert(startDisk, startBlock, tempbuf);
				}*/
				memcpy(tempbuf, write_buf + usedlen, 256); // copy all
				usedlen += 256;
				curAddr += 256;
				jbod_client_operation(op(startDisk, 0, JBOD_SEEK_TO_DISK, 0), NULL); // reseek
				jbod_client_operation(op(0, startBlock, JBOD_SEEK_TO_BLOCK, 0), NULL);
				jbod_client_operation(op(0, 0, JBOD_WRITE_BLOCK, 0), tempbuf); // write the temper buffer into block
																			   // cache_insert(startDisk, startBlock, tempbuf);
			}
		}
		if (cache_updated == 1)
		{
			cache_update(startDisk, startBlock, tempbuf);
		}
		else
		{
			cache_insert(startDisk, startBlock, tempbuf);
		} // printf("write:%d %d ", curAddr, usedlen);
	}
	free(tempbuf);
	tempbuf = NULL;
	return usedlen;
}