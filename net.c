#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure.
It may need to call the system call "read" multiple times to reach the given size len.
*/
static bool nread(int fd, int len, uint8_t *buf)
{
  // Keep track of the number of bytes read so far
  int r_byte = 0;
  // Continue reading until we have read 'len' bytes
  while (r_byte < len)
  {
    int num = read(fd, &buf[r_byte], len - r_byte);
    // If read fails, return false
    if (num == -1)
      return false;
    // Update the number of bytes read so far
    r_byte += num;
  }

  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf)
{
  int w_byte = 0;
  while (w_byte < len)
  {
    int num = write(fd, &buf[w_byte], len - w_byte);
    if (num == -1)
      return false;
    w_byte += num;
  }

  return true;
}

/* Through this function call the client attempts to receive a packet from sd
(i.e., receiving a response from the server.). It happens after the client previously
forwarded a jbod operation call via a request message to the server.
It returns true on success and false on failure.
The values of the parameters (including op, ret, block) will be returned to the caller of this function:

op - the address to store the jbod "opcode"
ret - the address to store the info code (lowest bit represents the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represent whether data block exists after HEADER_LEN.)
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first),
and then use the length field in the header to determine whether it is needed to read
a block of data from the server. You may use the above nread function here.
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block)
{
  uint8_t packet[HEADER_LEN];
  // Read in data from packet, if fails, abort
  bool status = nread(sd, HEADER_LEN, packet);
  if (status == false)
  {
    return false;
  }

  // Copy the opcode and return code from the packet into the given variables
  memcpy(op, packet, sizeof(*op));
  memcpy(ret, packet + sizeof(*op), sizeof(*ret));

  // If the return code has the first bit set to 1, return false (error)
  if (*ret & 1)
  {
    return false;
  }

  if (*ret & 2)
  {
    status = nread(sd, JBOD_BLOCK_SIZE, block);
    if (status == false)
    {
      return false;
    }
  }

  return true;
}
/* The client attempts to send a jbod request packet to sd (i.e., the server socket here);
returns true on success and false on failure.

op - the opcode.
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block)
{
  uint8_t packet[HEADER_LEN + 256];
  uint32_t bitmask = 0x3f;
  bitmask <<= 12;
  uint32_t cmd = op & bitmask;
  cmd >>= 12;
  uint32_t t_op = htonl(op);
  if (cmd == JBOD_WRITE_BLOCK)
  {
    uint8_t ret = 2;

    // Copy the opcode and ret code into the packet
    memcpy(packet, &t_op, 4);
    memcpy(packet + 4, &ret, 1);
    // Copy the data block into the packet
    memcpy(packet + HEADER_LEN, block, 256);
    // Send the packet
    return nwrite(sd, HEADER_LEN + 256, packet);
  }
  else
  {
    // Copy the opcode into the packet
    memcpy(packet, &t_op, HEADER_LEN);
    // Send the packet
    return nwrite(sd, HEADER_LEN, packet);
  }
  return true;
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not.
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
 */
bool jbod_connect(const char *ip, uint16_t port)
{
  // Setup address information
  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) == 0)
  {
    return false;
  }

  // Create socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1)
  {
    return false;
  }

  // Connect to server
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1)
  {
    return false;
  }
  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void)
{
  close(cli_sd); // Close socket
  cli_sd = -1;
}

/* sends the JBOD operation to the server (use the send_packet function) and receives
(use the recv_packet function) and processes the response.

The meaning of each parameter is the same as in the original jbod_operation function.
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block)
{ // Send out request, check for failure
  bool status = send_packet(cli_sd, op, block);
  if (status == false)
  {
    return -1;
  }
  // Prepare variables to store response in
  uint8_t ret;
  // Get response, check for failure
  status = recv_packet(cli_sd, &op, &ret, block);
  if (status == false)
  {
    return -1;
  }

  return 0;
}