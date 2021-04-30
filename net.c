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

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
static bool nread(int fd, int len, uint8_t *buf) {
  int data_read = 0;
  while (data_read < len)
    {
      int n = read(fd, &buf[data_read], len - data_read);
      if (n < 0)
	{
	  return false;
	}
      data_read += n;
    }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
static bool nwrite(int fd, int len, uint8_t *buf) {
  int data_written = 0;
  while (data_written < len)
  {
    int n = write(fd, &buf[data_written], len - data_written);
    if (n < 0)
      {
	return false;
      }
    data_written += n;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint16_t nlen;
  uint8_t header[HEADER_LEN];

  if (!nread(fd, HEADER_LEN, header))
    {
      return false;
    }

  int offset = 0;
  memcpy(&len, header + offset, sizeof(len));
  offset += sizeof(len);
  memcpy(op, header + offset, sizeof(*op));
  offset += sizeof(*op);
  memcpy(ret, header + offset, sizeof(*ret));
  
  len = ntohs(len);
  *op = ntohl(*op);
  *ret = ntohs(*ret);

  
  if(len == 264) //checks to see if its a read operation, if it is then read extra 256
    {
      nread(fd, 256, block);
    }
  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

  uint8_t header[264];
  uint16_t len = HEADER_LEN;
  uint16_t opcode = op >> 26;

  if (opcode == JBOD_WRITE_BLOCK)
    {
      len += 256;
    }
  
  uint16_t new_len = htons(len);
  uint32_t new_op = htonl(op);
  
  memcpy(header, &new_len, sizeof(new_len));
  memcpy(header+sizeof(new_len), &new_op, sizeof(new_op)); //construct the packet host or network
  
  if (opcode == JBOD_WRITE_BLOCK)
    {
      memcpy(header+ HEADER_LEN, block, 256);
    }
  //writing the packet using nwrite
  return nwrite(sd, len, header);
}


/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. */
bool jbod_connect(const char *ip, uint16_t port) {
  
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1)
    {
      return false;
    }

  struct sockaddr_in caddr;
  
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);

  inet_aton(ip, &caddr.sin_addr);
  //connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr));
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr))== 0) 
    {
      return true;
    }
  return false;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server and receives and processes the
 * response. */
int jbod_client_operation(uint32_t op, uint8_t *block) {

  uint16_t ret; 
  send_packet(cli_sd, op, block);
  recv_packet(cli_sd, &op, &ret, block);

  return ret;
}
