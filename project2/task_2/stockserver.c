#include "csapp.h"

#define MAXSTOCK 1024
#define NTHREADS 20
#define SBUFSIZE 100

typedef struct {
	int *buf;		// buffer array
	int n;			// maximum number of slots
	int front;		// buf[(front+1) % n] is first item
	int rear;		// buf[rear % n] is last item
	sem_t mutex;	// protects accesses to buf
	sem_t slots;	// counts available slots
	sem_t items;	// counts available items
} sbuf_t;

typedef struct {
	int ID;				// stock id
	int left;			// number of left stock
	int price;			// stock price
	int readcnt;		// number of reading thread
	sem_t read_mutex;	// protect reading accesses to buf
	sem_t write_mutex;	// protect writing accesses to buf
} Stock;

typedef struct _Node{		// node of stock binary search tree
	struct _Node* left;		// left child
	struct _Node* right;	// right child
	Stock data;				// stock data
} Node;

void parseline(char*, char**);

// sbuf functions
void sbuf_init(sbuf_t *, int);
void sbuf_deinit(sbuf_t *);
void sbuf_insert(sbuf_t *, int);
int sbuf_remove(sbuf_t *);

void parseline(char*, char**);

void read_stock_table();

Node* insert_node(Node*, Node*);
void free_node(Node*);
FILE* store_stock_data();
void print_stock_data(Node*, char []);
Stock* find_stock(Node*, int, int*);
void reduce_stock(Stock*, int);
void add_stock(Stock*, int);

void *thread(void*);
void process_command(int);
void pthread_init();
int reduce_stock_t(Node*, int, int, int);
int add_stock_t(Node*, int, int, int);

void sigint_handler(int);

Node* root = NULL;	// root of stock BST
int listenfd;		// receive request by listenfd

sbuf_t sbuf;	// producer-consumer thread pool

static int byte_cnt;	// counts total bytes received by server
static sem_t mutex_t;	// protect thread in pool

int main(int argc, char **argv) 
{
    int connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
	pthread_t tid;

	Signal(SIGINT, sigint_handler);		// call sigint_handler function when receive ctrl+c
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
    }

	read_stock_table();	// read stock data from stock.txt and make BST
//	print_stock_data(root, buf);

    listenfd = Open_listenfd(argv[1]);
	sbuf_init(&sbuf, SBUFSIZE);			// buf to store request
	for (int i = 0; i < NTHREADS; i++)	// make threads pool
		Pthread_create(&tid, NULL, thread, NULL);

    while (1) {
		clientlen = sizeof(struct sockaddr_storage);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		sbuf_insert(&sbuf, connfd);		// insert connfd to sbuf
    }

	store_stock_data();
	Close(listenfd);

    exit(0);
}
/* $end echoserverimain */

void *thread(void *vargp)
{
	Pthread_detach(pthread_self());

	while (1) {
		int connfd = sbuf_remove(&sbuf);	// thread receive connfd and remove it from sbuf
		process_command(connfd);			// process command
		Close(connfd);
	}
}

void process_command(int connfd)
{
	int n;
	char buf[MAXLINE];
	rio_t rio;
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	Pthread_once(&once, pthread_init);
	Rio_readinitb(&rio, connfd);

	// read request from connfd
	while ((n = Rio_readlineb(&rio, buf, MAXLINE))) {
		byte_cnt += n;	// add total byte

		printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);

		if (!strncmp(buf, "show", 4)) {	// show
			memset(buf, 0, MAXLINE);	// cleaar buf
			print_stock_data(root, buf);	// write response to buf
			Rio_writen(connfd, buf, MAXLINE);
			continue;
		}
		else if (!strncmp(buf, "buy", 3)) {	// buy
			int stockID, reqCnt;	// get stockID

			sscanf(buf + 4, "%d %d", &stockID, &reqCnt);
			reduce_stock_t(root, connfd, stockID, reqCnt);
			continue;
			/*
			Stock* s = find_stock(root, stockID, &left);	// find stock and number of left stock
			if (!s) {	// stock id is not found
				Rio_writen(connfd, "", MAXLINE);
				return;
			}

			if (reqCnt > left) {	// if not enough left stock
				strcpy(buf, "Not enough left stocks\n");
			}
			else {	// enough stock
				reduce_stock_t(s, reqCnt);
				strcpy(buf, "[buy] success\n\0");
			}
			*/
		}
		else if (!strncmp(buf, "sell", 4)) {	// sell
			int stockID, reqCnt;	// get stockID

			sscanf(buf + 4, "%d %d", &stockID, &reqCnt);
			fflush(stdout);
			add_stock_t(root, connfd, stockID, reqCnt);
			
			/*
			Stock* s = find_stock(root, stockID, NULL);		// find stock and number of left stock
			if (!s) {	// if stock id is not found
				Rio_writen(connfd, "", MAXLINE);
				return;
			}

			add_stock_t(s, reqCnt);	// add stock
			strcpy(buf, "[sell] success\n");
			*/
			continue;
		}
		else if (!strncmp(buf, "exit", 4)) {	// exit
			return;
		}

		Rio_writen(connfd, buf, MAXLINE);	// send response to connfd
		memset(buf, 0, MAXLINE);	// cleaar buf
	}
}

void pthread_init()
{
	Sem_init(&mutex_t, 0, 1);
	byte_cnt = 0;
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

	free(root); // deallocate memory
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

		// init read, write semaphore
		Sem_init(&n->data.read_mutex, 0, 1);
		Sem_init(&n->data.write_mutex, 0, 1);

		// insert input node to BST
		root = insert_node(root, n);
	}

	fclose(fp);

	return;
}

int reduce_stock_t(Node* root, int connfd, int stockID, int reqCnt)
{
	if (!root)
		return 0;

	if (root->data.ID == stockID) {
		P(&root->data.write_mutex);
			if (root->data.left < reqCnt)
				Rio_writen(connfd, "Not enough left stock\n", MAXLINE);
			else {
				Rio_writen(connfd, "[buy] success\n", MAXLINE);
				root->data.left -= reqCnt;
			}
		V(&root->data.write_mutex);

		return 1;
	}
	if (root->data.ID > stockID)
		return reduce_stock_t(root->left, connfd, stockID, reqCnt);
	else
		return reduce_stock_t(root->right, connfd, stockID, reqCnt);
}

int add_stock_t(Node* root, int connfd, int stockID, int reqCnt)
{
	if (!root)
		return 0;

	if (root->data.ID == stockID) {
		P(&root->data.write_mutex);
		Rio_writen(connfd, "[sell] success\n", MAXLINE);
		root->data.left += reqCnt;
		V(&root->data.write_mutex);

		return 1;
	}
	if (root->data.ID > stockID)
		return add_stock_t(root->left, connfd, stockID, reqCnt);
	else
		return add_stock_t(root->right, connfd, stockID, reqCnt);

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
	char tmp[20];
	if (!root)	// if root is NULL< exit function
		return;

	P(&root->data.read_mutex);	// protect access to read
	root->data.readcnt++;
	if (root->data.readcnt == 1)	// if reader exists, block writer
		P(&root->data.write_mutex);
	V(&root->data.read_mutex);	// allow access to read

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


	P(&root->data.read_mutex);	// protect access to read
	root->data.readcnt--;
	if (root->data.readcnt == 0)	// if there is a blocked writer
		V(&root->data.write_mutex);	// allow access to write
	V(&root->data.read_mutex);	// allow access to read

	print_stock_data(root->left, buf);	// move to left child
	print_stock_data(root->right, buf);	// move to right child

	return;
}

Stock* find_stock(Node* root, int ID, int *left)
{
	Node* t = root;
	if (!root)	// if root is NULL, exit function
		return NULL;

	while (t) {	// while t is not null
		if (t->data.ID == ID) {
			Stock* ret;
			if (left)
				*left = t->data.left;
			P(&t->data.read_mutex);	// protect access to read
			t->data.readcnt++;
			if (t->data.readcnt == 1)	// if reader exists, block writer
				P(&t->data.write_mutex);
			V(&t->data.read_mutex);	// allow access to read

			ret = &t->data;

			P(&t->data.read_mutex);	// protect access to read
			t->data.readcnt--;
			if (t->data.readcnt == 0)	// if there is blocked writer
				V(&t->data.write_mutex);	// allow access to write
			V(&t->data.read_mutex);	// allow access to read

			return ret;
		}
		else if (t->data.ID < ID)	// decide next child
			t = t->right;
		else 
			t = t->left;
	}
	return NULL;
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
	FILE *fp = store_stock_data(root, NULL);	// store updated stock data
	if (fp)	// if fp is not NULL
		fclose(fp);	// close fp
	free_node(root);	// deallocate BST memory
	sbuf_deinit(&sbuf);

	exit(0);	// terminate process
}

void parseline(char* buf, char **argv)
{
	char *delim;
	int argc;

	buf[strlen(buf) - 1] = ' ';

	while(*buf && (*buf == ' '))
		buf++;

	argc = 0;
	while ((delim = strchr(buf, ' '))) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' '))
			buf++;
	}
	argv[argc] = NULL;
}

/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int)); 
    sp->n = n;                       /* Buffer holds max of n items */
    sp->front = sp->rear = 0;        /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1);      /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n);      /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0);      /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item;   /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp)
{
	int item;
	P(&sp->items);
	P(&sp->mutex);
	item = sp->buf[(++sp->front) % (sp ->n)];
	V(&sp->mutex);
	V(&sp->slots);

	return item;
}
