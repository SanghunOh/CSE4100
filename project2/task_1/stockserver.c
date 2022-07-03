#include "csapp.h"

#define MAXSTOCK 1024

typedef struct {	// connected descriptor pool
	int maxfd;
	fd_set read_set;
	fd_set ready_set;
	int nready;	// number of ready descriptor
	int maxi;
	int clientfd[FD_SETSIZE];	// active socket descriptors
	rio_t clientrio[FD_SETSIZE];	// read buffers
} pool;

typedef struct {	// stock item
	int ID;		// stock id
	int left;	// number of left stock
	int price;	// stock price
} Stock;

typedef struct _Node{		// node of stock binary search tree
	struct _Node* left;		// left child
	struct _Node* right;	// right child
	Stock data;				// stock data
} Node;

void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);

void parseline(char*, char**);

void read_stock_table();

Node* insert_node(Node* root, Node* n);
void free_node(Node*);
FILE* store_stock_data(Node*, FILE*);
void print_stock_data(Node*, char []);
Stock* find_stock(Node*, int);
void reduce_stock(Stock*, int);
void add_stock(Stock*, int);

void sigint_handler(int);

int byte_cnt = 0;	// counts total bytes received by server
Node* root = NULL;	// root of stock BST
int listenfd;		// receive request by listenfd

int main(int argc, char **argv) 
{
    int connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
	static pool pool;
	char client_hostname[MAXLINE], client_port[MAXLINE];

	Signal(SIGINT, sigint_handler);	// call sigint_hander function when receive ctrl+c 
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	read_stock_table();	// read stock data from stock.txt and make BST

	listenfd = Open_listenfd(argv[1]);
	init_pool(listenfd, &pool);

    while (1) {
		// wait for listening/connected descriptor to become ready
		pool.ready_set = pool.read_set;
		pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);
		
		// if listening descriptor ready, add new client to pool
		if (FD_ISSET(listenfd, &pool.ready_set)) {
			clientlen = sizeof(struct sockaddr_storage);
			connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
			Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
			printf("Connected to (%s, %s)\n", client_hostname, client_port);
			add_client(connfd, &pool);
		}

		// process input from each ready connected descriptor
		check_clients(&pool);
    }

	store_stock_data(root, NULL);
	Close(listenfd);

	return 0;
}
/* $end echoserverimain */

void init_pool(int listenfd, pool *p)
{
	// initailly there are no connected descriptor
	int i;
	p->maxi = -1;

	for(i = 0; i < FD_SETSIZE; i++)
		p->clientfd[i] = -1;

	// initially, listenfd is only member of select read set
	p->maxfd = listenfd;
	FD_ZERO(&p->read_set);
	FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool *p)
{
	int i;
	p->nready--;
	for (i = 0; i < FD_SETSIZE; i++) {
		if (p->clientfd[i] < 0) {
			// add connected descriptor to the pool
			p->clientfd[i] = connfd;
			Rio_readinitb(&p->clientrio[i], connfd);

			// add the descriptor to descriptor set
			FD_SET(connfd, &p->read_set);

			// update max descriptor and pool high water mark
			if (connfd > p->maxfd)
				p->maxfd = connfd;
			if (i > p->maxi)
				p->maxi = i;
			break;
		}
	}

	if (i == FD_SETSIZE)	// couldn't find an empty slot
		app_error("add_client error: Too many clients");
}

void check_clients(pool *p)
{
	int i, connfd, n;
	char buf[MAXLINE];
	rio_t rio;

	for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
		connfd = p->clientfd[i];
		rio = p->clientrio[i];

		// if the descriptor is ready, send proper response
		if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
			p->nready--;
			memset((void*)buf, 0, MAXLINE);		// clear buf
			if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {	// read request from connfd
				char *word[5];
				char _buf[MAXLINE];
				byte_cnt += n;
				printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);

				strcpy(_buf, buf);
				parseline(_buf, word);	// parse words from buf
				
				if (!strcmp(word[0], "show")) {	// show
					strcpy(buf, "");
					print_stock_data(root, buf);	// write response to buf
					Rio_writen(connfd, buf, MAXLINE);	// send response to connfd
				}
				else if (!strcmp(word[0], "buy")) {	// buy
					int stockID = atoi(word[1]);	// get stockID
					int reqCnt = atoi(word[2]);		// request count

					Stock* s = find_stock(root, stockID);	// find stock
					if (!s) {	// stock id not found
						Rio_writen(connfd, "", MAXLINE);

						return;
					}

					if (reqCnt > s->left) {		// if not enough left stock
						Rio_writen(connfd, "Not enough left stocks\n", MAXLINE);
					}
					else {	// enough stock
						reduce_stock(s, reqCnt);
						Rio_writen(connfd, "[buy] success\n", MAXLINE);
					}
				}
				else if (!strcmp(word[0], "sell")) {	// sell
					int stockID = atoi(word[1]);	// get stockID
					int reqCnt = atoi(word[2]);		// request count

					Stock* s = find_stock(root, stockID);	// find stock
					if (!s) {	// stock id not found
						Rio_writen(connfd, "", MAXLINE);

						return;
					}

					add_stock(s, reqCnt);		// add stock
					Rio_writen(connfd, "[sell] success\n", MAXLINE);
				}
				else if (!strcmp(word[0], "exit")) {	// exit
					Close(connfd);
					FD_CLR(connfd, &p->read_set);
					p->clientfd[i] = -1;
				}
			}
			else {
				Close(connfd);
				FD_CLR(connfd, &p->read_set);
				p->clientfd[i] = -1;
			}
		}
	}
}

Node* insert_node(Node* root, Node* n)
{
	if (root == NULL) {	// if root is NULL
		root = n;	// set root to n
		return root;
	}

	if (n->data.ID <= root->data.ID) {	// move to left child
		root->left = insert_node(root->left, n);
	}
	else {	// move to right child
		root->right = insert_node(root->right, n);
	}
	return root;
}

void free_node(Node* root)
{
	if (!root)	// if root is NULL, exit function
		return;

	free_node(root->left);	// move to left child
	free_node(root->right);	// move to right child

	free(root);	// deallocate memory
}

void read_stock_table()
{
	FILE* fp;
	// open stock.txt file
	fp = fopen("stock.txt", "r");
	int ID, left, price;

	// read all stocks from stored data
	while (fscanf(fp, "%d%d%d", &ID, &left, &price) != EOF){
		Node* n = (Node *)malloc(sizeof(Node));
		n->left = n->right = NULL;
		n->data.ID = ID;
		n->data.left = left;
		n->data.price = price;

		// insert input node to BST
		root = insert_node(root, n);
	}

	fclose(fp);

	return;
}

FILE* store_stock_data(Node* root, FILE* fp)
{
	if (!root)	// if root is NULL, exit function
		return NULL;
	if (!fp)	// if file descriptor is NULL, open stock.txt file
		fp = fopen("stock.txt", "w");

	fprintf(fp, "%d %d %d\n", root->data.ID, root->data.left, root->data.price);	// write data
	store_stock_data(root->left, fp);	// traverse to left child
	store_stock_data(root->right, fp);	// traverse to right child

	return fp;
}

void print_stock_data(Node* root, char buf[])
{
	if (!root)	// if root is NULL, exit function
		return;
	char tmp[20];
	
	// write stock data
	sprintf(tmp, "%d", root->data.ID);
	strcat(buf, tmp);
	strcat(buf, " ");
	sprintf(tmp, "%d", root->data.left);
	strcat(buf, tmp);
	strcat(buf, " ");
	sprintf(tmp, "%d", root->data.price);
	strcat(buf, tmp);
	strcat(buf, " ");
	strcat(buf, "\n");
	print_stock_data(root->left, buf);	// traverse to left child
	print_stock_data(root->right, buf);	// traverse to right child

	return;
}

Stock* find_stock(Node* root, int ID)
{
	if (!root)	// if root is NULL, exit function
		return NULL;
	if (root->data.ID == ID)	// if found
		return &root->data;		// return stock data
	else if (ID < root->data.ID)	// if ID is less than ID of current node
		return find_stock(root->left, ID);	// move to left child
	else	// if ID is bigger than ID of current node
		return find_stock(root->right, ID);	// move to right child
}

void reduce_stock(Stock* s, int cnt)
{
	// subtract cnt
	s->left -= cnt;
}

void add_stock(Stock* s, int cnt)
{
	// add cnt
	s->left += cnt;
}

void sigint_handler(int sig)
{
	Close(listenfd);	// close listen file descriptor
	Sio_puts("\n");
	FILE* fp = store_stock_data(root, NULL);	// store updated stock data
	if (fp)	// if fp is not NULL
		fclose(fp);	// close fp
	free_node(root);	// deallocate BST memory

	exit(0);	// terminate process
}

void parseline(char* buf, char **argv)
{
	char *delim;
	int argc;

	buf[strlen(buf) - 1] = ' ';		// change '\n' to ' '

	while(*buf && (*buf == ' '))	// ignore blank
		buf++;

	argc = 0;
	while ((delim = strchr(buf, ' '))) {	// find left blank
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' '))
			buf++;
	}
	argv[argc] = NULL;
}
