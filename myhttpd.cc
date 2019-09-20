const char * usage =
"                                                               \n"
"daytime-server:                                                \n"
"                                                               \n"
"Simple server program that shows how to use socket calls       \n"
"in the server side.                                            \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   daytime-server <port>                                       \n"
"                                                               \n"
"Where 1024 < port < 65536.             \n"
"                                                               \n"
"In another window type:                                       \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where daytime-server  \n"
"is running. <port> is the port number you used when you run   \n"
"daytime-server.                                               \n"
"                                                               \n"
"Then type your name and return. You will get a greeting and   \n"
"the time of the day.                                          \n"
"                                                               \n";


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

typedef struct dirF
{
  struct dirent *dir;
  char *Path;
  char *Dpath;
  struct stat file_At;
}dirF;

pthread_mutex_t mutex;
pthread_mutexattr_t mattr;
time_t startime;
string contentType;

int debug = 1;
int QueueLength = 5;
//global var for mode 1 for -f; 2 for -t; 3 for -p; 0 for basic
int mode = 0; 
//port number
int port = 0;
//flag for compare helper; 0 for asc, 1 for des;
int cmp_flag = 0;
//number of request
int num_req = 0;
// Processes time request
void processRequest( int socket );
void dispatchHTTP(int fd);
void *loopthread(int fd);
void poolOfThreads(int masterSocket);
void CGI(int socket, string docpath, char* rpp, char *args);
char * write_parent(int socket, string docpath, const char * main_msg);
void sorting (dirF **files, int count_file, string argument);
int cmp_name(const void *f1, const void * f2);
int cmp_mod(const void *f1, const void * f2);
int cmp_size(const void *f1, const void * f2);
void Log(int socket, char *rpp);
void send_stats(int socket, char *rpp);
string get_argument(string docpath);
extern "C" void killzombie(int sig);

//compare helper
int cmp_name (const void *f1, const void *f2)
{
  struct dirF *dirf1 = *(struct dirF **)f1;
  struct dirF *dirf2 = *(struct dirF **)f2;
  if (cmp_flag == 0){
    return strcmp(dirf1->dir->d_name, dirf2->dir->d_name);
  }
  else
  {
    return strcmp(dirf2->dir->d_name, dirf1->dir->d_name); 
  }
}

int cmp_mod(const void *f1, const void *f2)
{
  struct dirF *dirf1 = *(struct dirF **)f1;
  struct dirF *dirf2 = *(struct dirF **)f2;
  //compare the time of last modification
  if (cmp_flag == 0){
    return strcmp(ctime(&(dirf1->file_At.st_mtime)), ctime(&(dirf2->file_At.st_mtime)));
  }
  else
  {
    return strcmp(ctime(&(dirf2->file_At.st_mtime)), ctime(&(dirf1->file_At.st_mtime))); 
  }
}

int cmp_size(const void *f1, const void *f2)
{
  struct dirF *dirf1 = *(struct dirF **)f1;
  struct dirF *dirf2 = *(struct dirF **)f2;
  if (cmp_flag == 0){
    return dirf1->file_At.st_size - dirf2->file_At.st_size;
  }
  else
  {
    return dirf2->file_At.st_size - dirf1->file_At.st_size;
  }
}

int cmp_des_name (const void *f1, const void *f2)
{
  struct dirF *dirf1 = *(struct dirF **)f1;
  struct dirF *dirf2 = *(struct dirF **)f2;
  return strcmp(dirf1->dir->d_name, dirf2->dir->d_name);
}

int
main( int argc, char ** argv )
{

  time(&startime);
  // Print usage if not enough arguments
  if ( argc < 3 ) {
    if (argc == 1)
    {
        port = 6789;
        
    }
    else if (argc == 2)
    {
        // Get the port from the arguments
        port  = atoi( argv[1] );
    }
    else
    {
        fprintf( stderr, "%s", usage );
        exit( -1 );
    }
  }
  else if (argc == 3)
  {
    //get the concurrency type
    if (argv[1][0] == '-')
    {
        if (argv[1][1] == 'f')
        {
            //create a new process for each request
            mode = 1;
        }
        else if (argv[1][1] == 't')
        {
            //create a new thread for each request
            mode = 2;
        }
        else if (argv[1][1] == 'p')
        {
            //pool of threads
            mode = 3;
        }
        else
        {
            fprintf( stderr, "%s", usage);
            exit(-1);
        }
    }
    port  = atoi( argv[2] );
  }
  else
  {
    fprintf( stderr, "%s", usage );
    exit(-1);
  }
  
  struct sigaction sa2;
  sa2.sa_handler = killzombie;
  sigemptyset(&sa2.sa_mask);
  sa2.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD,&sa2, NULL))
  {
    perror("sigaction");
    exit(2);
  }


  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);
  
  // Allocate a socket
  int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }
  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the sae port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
           (char *) &optval, sizeof( int ) );
   
  // Bind the socket to the IP address and port
  int error = bind( masterSocket,
        (struct sockaddr *)&serverIPAddress,
        sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }
  
  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen( masterSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }
  if (mode == 3)
  {
    poolOfThreads(masterSocket);
  }
  else
  {
    while ( 1 ) {
        // Accept incoming connections
        struct sockaddr_in clientIPAddress;
        int alen = sizeof( clientIPAddress );
        int slaveSocket = accept( masterSocket,
                (struct sockaddr *)&clientIPAddress,
                (socklen_t*)&alen);
        //needs modify, keep server running if there is an error
        if ( slaveSocket < 0 ) {
          perror( "accept" );
          exit( -1 );
        }
        num_req++;
        if (mode == 0)
        {
            processRequest(slaveSocket);
            close(slaveSocket);
        }
        //process based -f
        else if (mode == 1)
        {
          pid_t slave = fork();
          if (slave == 0)
          {
            processRequest(slaveSocket);
            close(slaveSocket);
            exit(EXIT_SUCCESS); 
          }
          close(slaveSocket);
        }
        //thread based 
        else if (mode == 2)
        {
          pthread_t thread;
          pthread_attr_t attr;
          pthread_attr_init(&attr);
          pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
          pthread_create(&thread, &attr, (void* (*)(void*))dispatchHTTP, (void *) slaveSocket);
        }
     }
  }
}
void processRequest(int socket)
{
  const int size = 1024;
  int length = 0;
  int n;
  int gotGet = 0;
  int docpath_index = 0;

  unsigned char newChar;
  unsigned char oldChar = 0;
  unsigned char olldchar = 0;
  unsigned char ollldchar = 0;

  string request;
  string request_type;
  string docpath;
  string argument;

  while (length < size && (n = read(socket, &newChar, sizeof(newChar))) > 0)
  {
    if (newChar == ' ')
    {
      if (!gotGet)
      {
        gotGet = length;
        request_type = request;
      }
      else if (!docpath_index)
      {
        docpath_index = length;
        docpath = request.substr(gotGet+1, length-gotGet-1);
      }
    }
    else if (newChar == '\n' && oldChar == '\r' && olldchar == '\n' && ollldchar == '\r')
    {
      break;
    }
    else
    {
      ollldchar = olldchar;
      olldchar = oldChar;
      oldChar = newChar;
    }
    ++length;
    request += newChar;
  }
  if (get_argument(docpath) != "")
  {
    argument = get_argument(docpath);
    size_t pos = docpath.find(argument);    
    docpath = docpath.substr(0, pos);
  }
  else
  {
    argument = "";
  }
  
  char cwd[size + 1] = {0};
  getcwd(cwd, sizeof(cwd));
  string address;
  address = cwd;

  if (docpath.substr(0, strlen("/icons")) == "/icons")
  {
    address += "/http-root-dir";
    address += docpath;
  }
  else if (docpath.substr(0, strlen("/htdocs")) == "/htdocs")
  {
    address += "/http-root-dir";
    address += docpath;
  }
  else if (docpath.substr(0, strlen("/cgi-bin")) == "/cgi-bin")
  {
    address += "/http-root-dir"; 
    address += docpath;
  }
  else
  {
    if (strcmp(docpath.c_str(),"/") == 0 && docpath.length() == 1)
    {
      address +="/http-root-dir/htdocs/index.html";
    }
    else{
      address += "/http-root-dir/htdocs";
      address +=  docpath;
    }
  }  
  char rpp[size + 1];
  char *res = realpath(address.c_str(), rpp);

  if(strlen(rpp) < (strlen(cwd) + strlen("/http-root-dir")))
  {
    const char * sig = "Access Denied\r\n";
    const char * msg = "HTTP/1.1 403 Forbidden\r\nServer: CS 252 lab5/1.0\r\n\r\n";
    write(socket, msg, strlen(msg));
    write(socket, sig, strlen(sig));
    return;
  }
  else
  {
      Log(socket, rpp);
      if (strstr(rpp, ".html") || strstr(rpp, ".html/"))
      {
        contentType = "text/html";
      }
      else if (strstr(rpp, ".jpg") || strstr(rpp, ".jpg/"))
      {
        contentType = "image/jpeg";
      }
      else if (strstr(rpp, ".png") || strstr(rpp, ".png/"))
      {
        contentType = "image/png";
      }
      else if (strstr(rpp, ".gif") || strstr(rpp, ".gif/"))
      {
        contentType = "image/gif";
      }
      else if (strstr(rpp, ".svg") || strstr(rpp, ".svg/"))
      {
        contentType = "image/svg+xml";
      }
      else
      {
        contentType = "text/plain";
      }
    
      if (strstr(rpp,"?"))
      {
        rpp [strlen(rpp) - 1] = '\0';
      }
      DIR *directory = opendir(rpp);
      if (directory != NULL)
      { 
          const char * temp = address.c_str();
          const char * main_msg;
          const char * parent;
          const char * msg1 = "HTTP/1.0 200 Document follows\r\nServer: CS 252 lab5/1.0\r\nContent-type: text/html\r\n\r\n";
          write(socket, msg1, strlen(msg1));
          const char * msg2 = "<html>\n <head>\n  <title>Index of ";
          write(socket, msg2, strlen(msg2));
          write(socket, temp, strlen(temp));
          const char * msg3 = "</title>\n </head>\n <body>\n<h1>Index of ";
          write(socket, msg3, strlen(msg3));
          write(socket, temp, strlen(temp));
          const char * msg4 = "</h1>\n<table><tr>";
          write(socket, msg4, strlen(msg4));

          if (argument == "")
          {
              main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=D\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
          }
          else if (argument[2] == 'N'){
              if (argument[6] == 'A') {
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=D\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
              else
              {
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
          }
          else if (argument[2] == 'M'){
              if (argument[6] == 'A'){
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=D\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
              else
              {
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
          }
          else if (argument[2] == 'S')
          {
              if (argument[6] == 'A'){
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=D\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
              else
              {
                main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
              }
          }
          else
          {
            main_msg = "<tr><th><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=D\">Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>";
          }
          //write the parent directory

          parent = write_parent(socket, docpath, main_msg);

          dirF **files = (dirF**)malloc(100 * sizeof(dirF));
          struct dirent *ent;
          int count_file = 0;

          while ((ent = readdir(directory)) != NULL)
          {
            dirF *file = (dirF *)malloc(100 * sizeof(dirF));
            char *path = (char *)malloc(1024 * sizeof(char));
            char *dpath = (char *)malloc(1024 * sizeof(char));
            struct stat buffer;
            file->dir = ent;
            if (file->dir->d_name[0] != '.')
            {
              strcpy(path, rpp);
              /*if (strstr(path, "/") == NULL)
              {
                strcat(path, "/");
              }*/
              strcat(path, ent->d_name);
              file->Path = path;
              strcpy(dpath, docpath.c_str());
              /*if (strstr(dpath, "/") == NULL)
              {
                strcat(dpath, "/");
              }*/
              strcat(dpath, ent->d_name);
              file->Dpath = dpath;
              stat(path, &buffer);
              file->file_At = buffer;
              files[count_file] = file;
              count_file++;
            }
            else
            { 
                docpath += "/";
            }
          }
          sorting (files, count_file, argument);
          for (int i =0; i < count_file; i++)
          {
            if (files[i]->dir->d_type == DT_DIR)
            {
              main_msg = "<tr><td valign=\"top\"><img src=\"/icons/menu.gif\" alt=\"[DIR]\"></td><td><a href=\"";
            }
            else
            {
              if (strstr(files[i]->Path, ".html") || strstr(files[i]->Path, ".html/"))
              {
                main_msg = "<tr><td valign=\"top\"><img src=\"/icons/text.gif\" alt=\"[   ]\"></td><td><a href=\"";
              }
              else if (strstr(files[i]->Path, ".git") || strstr(files[i]->Path, ".gif/"))
              {
                main_msg = "<tr><td valign=\"top\"><img src=\"/icons/image.gif\" alt=\"[   ]\"></td><td><a href=\"";
              }
              else if (strstr(files[i]->Path, ".svg") || strstr(files[i]->Path, ".svg/"))
              {
                main_msg = "<tr><td valign=\"top\"><img src=\"/icons/image.gif\" alt=\"[   ]\"></td><td><a href=\"";
              }
              else if (strstr(files[i]->Path, ".xbm") || strstr(files[i]->Path, ".xmb/"))
              {
                main_msg = "<tr><td valign=\"top\"><img src=\"/icons/image.gif\" alt=\"[   ]\"></td><td><a href=\"";
              }
              else
              {
                main_msg = "<tr><td valign=\"top\"><img src=\"/icons/unknown.gif\" alt=\"[   ]\"></td><td><a href=\"";
              }
            }
            write(socket, main_msg, strlen(main_msg));
            write(socket, files[i]->Dpath, strlen(files[i]->Dpath));
            write(socket, "\">", strlen("\">"));
            write(socket, files[i]->dir->d_name, strlen(files[i]->dir->d_name));
            main_msg = ctime(&(files[i]->file_At.st_mtime));
            write(socket, "</a>               </td><td align=\"right\">", strlen("</a>               </td><td align=\"right\">"));
            write(socket, main_msg, strlen(main_msg));
            write(socket, "</td><td align=\"right\">", strlen("</td><td align=\"right\">"));
            write(socket, "</td><td>&nbsp;</td></tr>\n", strlen("</td><td>&nbsp;</td></tr>\n"));
          }
          main_msg = std::to_string(port).c_str();
          write(socket, "<tr><th colspan=\"5\"><hr></th></tr>\n", strlen("<tr><th colspan=\"5\"><hr></th></tr>\n"));
          write(socket, "</table>\n<address>Apache/2.2.24 (Unix) mod_ssl/2.2.2OpenSSL/0.9.8ze Server at www.cs.purdue.edu Port ", strlen("</table>\n<address>Apache/2.2.24 (Unix) mod_ssl/2.2.2OpenSSL/0.9.8ze Server at www.cs.purdue.edu Port "));
          write(socket, main_msg, strlen(main_msg));
          write(socket, "</address>\n</body></html>\n", strlen("</address>\n</body></html>\n"));
      }

      else
      {
          FILE *document = fopen(address.c_str(), "rb");
          if (document > 0)
          { 
            const char * protocol = "HTTP/1.0 200 Document follows";
            const char * crlf = "\r\n";
            const char * server = "Server: CS 252 lab5/1.0";
            const char * content_type = "Content-type: ";

            write(socket, protocol, strlen(protocol));
            write(socket, crlf, strlen(crlf));
            write(socket, server, strlen(server));
            write(socket, crlf, strlen(crlf));
            write(socket, content_type, strlen(content_type));
            write(socket, crlf, strlen(crlf));
            write(socket, crlf, strlen(crlf));
            long count = 0;
            char ch;
            while (count = read(fileno(document), &ch,sizeof(ch)))
            {
                if (write(socket, &ch, sizeof(ch)) != count)
                {
                    perror("write");
                }
            }
            fclose(document);
          }
          else
          {
            if (strstr(rpp,"/stats"))
            {
              send_stats(socket, rpp);
            }
            else
            {
              const char * notFound = "HTTP/1.0 404 File Not Found";
              const char * crlf = "\r\n";
              const char * server = "Server: CS 252 lab5/1.0";
              const char * content_type = "Content-type: ";
              const char * err_msg = "File not Found";
              
              write(socket, notFound, strlen(notFound));
              write(socket, crlf, strlen(crlf));
              write(socket, server, strlen(server));
              write(socket, crlf, strlen(crlf));
              write(socket, content_type, strlen(content_type));
              write(socket, crlf, strlen(crlf));
              write(socket, crlf, strlen(crlf));
              write(socket, err_msg, strlen(err_msg));
            }
          }
      }
  }
}


void dispatchHTTP (int socket)
{
  processRequest(socket);
  close(socket);
}

void poolOfThreads(int masterSocket)
{
  pthread_t tid[5]; 
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  for (int i = 0; i < 4; i++)
  {
    pthread_create(&tid[i], NULL, (void * (*)(void *))loopthread, (void *)masterSocket);
  }
  loopthread(masterSocket);
}

void *loopthread (int fd)
{
  while (1) {

    pthread_mutex_lock(&mutex);
    struct sockaddr_in clientIPAddress; 
    int alen = sizeof(clientIPAddress);
    int slaveSocket = accept(fd, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
    ++num_req;
    pthread_mutex_unlock(&mutex);
    if (slaveSocket >= 0)
    {
       dispatchHTTP(slaveSocket);
    }
  }
}

void CGI(int socket, string docpath, char* rpp, char *args)
{

  const char *msg = "HTTP/1.0 200 Document follows\r\nServer: CS 252 lab5/1.0";
  write(socket, msg, strlen(msg));

  if (!strstr(rpp, ".so"))
  {
    pid_t ret = fork();
    if (!ret)
    {
      cout << "0 " << endl;
      if (args != NULL)
      {
        cout << "1 " << endl;
        setenv("REQUEST_METHOD", "GET", 1);
        setenv("QUERY_STRING", args, 1);
      }
      dup2(socket, 1);
      close(socket);
      if (args)
      {
        cout << "3" << endl;
        execl(rpp, args, 0, (char*)0);
      }
      else
      {
        execl(rpp, NULL, 0, (char*)0);
      }
      exit(0);
    }
    waitpid(ret, NULL, 0);
    shutdown(socket, SHUT_RDWR);
    close(socket);

  }
  else
  {
    cout << " zai na li " << endl;
  }
}

char * write_parent(int socket, string docpath, const char * main_msg)
{
  int length = docpath.length();
  int count = 1;

  while (docpath[length - count - 1] != '/') {
    ++count;
  }
  char parent[length-count+1] = {0};
  for (int i = 0; i < length - count; i++) {
    parent[i] = docpath[i];
  }
  parent[length - count] = '\0';

  write(socket, main_msg, strlen(main_msg));
  const char * msg1 = "<tr><td valign=\"top\"><img src=\"/icons/menu.gif\" alt=\"[DIR]\"></td><td><a href=\"";
  write(socket, msg1, strlen(msg1));
  write(socket, parent, strlen(parent));
  const char * msg2 = "\">Parent Directory</a>       </td><td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>";
  write(socket, msg2, strlen(msg2));

  return parent;
}

string get_argument (string docpath)
{
  int flag = 0;
  string argument;
  for (int i = 0; i < docpath.length(); i++)
  {
    if (docpath[i] == '?')
    {
      flag = 1;
    }
    else if (flag == 1)
    {
      argument += docpath[i]; 
    }
  }
  if (flag == 0){ return "";}
  else { return argument;}
}

void sorting (dirF **files, int count_file, string argument)
{
  if (argument == "")
  {
    //default ascending name
    qsort((void**)files, count_file, sizeof(struct dirent*), cmp_name);
  }
  else if (argument[6] == 'A')
  {
    cmp_flag = 0;
    if (argument[2] == 'M')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_mod);
    }
    else if (argument[2] == 'S')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_size); 
    }
    else if (argument[2] == 'N')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_name);  
    }
  }
  else if (argument[6] == 'D')
  {
    cmp_flag = 1;
    if (argument[2] == 'M')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_mod);
    }
    else if (argument[2] == 'S')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_size); 
    }
    else if (argument[2] == 'N')
    {
      qsort(files, count_file, sizeof(struct dirent*), cmp_name);  
    }
  }
}

void Log(int socket, char * rpp)
{
  int fd;
  char *cwd = (char *)malloc(1024 * sizeof (char));
  char *file_path = (char* )malloc(1024 * sizeof (char));
  char *new_log = (char *)malloc (1024 * sizeof (char));
  char *addr = (char *)malloc (1024 * sizeof (char));
  getcwd (cwd, sizeof(cwd));

  strcpy(file_path, cwd);
  strcat(file_path, "/http-root-dir/htdocs/logs");
  struct sockaddr_in sk;
  int socket_size = sizeof(sk);

  getsockname(socket, (struct sockaddr*) &sk, (socklen_t *)&socket_size);
  addr = inet_ntoa(sk.sin_addr);

  sprintf(new_log, "%s", "Host: ");
  sprintf(new_log, "%s",  addr);
  sprintf(new_log, "%s","\tDirectory: ");
  sprintf(new_log, "%s", rpp);
  sprintf(new_log, "%s", "\n");
  fd = open(file_path, O_WRONLY | O_APPEND, 0666);
  write(fd, new_log, strlen(new_log));
}

void send_stats(int socket, char * rpp)
{
  const char *msg4 = "</h4>";
  const char *msg1 = "HTTP/1.0 200 Document follows\r\nServer: CS 252 lab5/1.0\r\nContent-type: text/html\r\n\r\n<html>\n <head>\n  <title>Statistics</title>\n </head>\n <body>\n<h1>Statistics</h1>\n";
  write(socket, msg1, strlen(msg1));
  const char *name = "<h3>The names of the student who wrote the project:</h3><h4>";
  write(socket, name, strlen(name));
  const char *name_user = "Shiwen Xu";
  write(socket, name_user, strlen(name_user));
  write(socket, msg4, strlen(msg4));
  const char *msg2 = "<h3>The time the server has been up:</h3><h4>";
  char *msg3 = (char*)malloc(50);
  time_t lastime;
  time(&lastime);
  double diff_time = difftime(lastime, startime);
  sprintf(msg3, "%f", diff_time);
  write(socket, msg2, strlen(msg2));
  write(socket, msg3, strlen(msg3));
  write(socket, msg4, strlen(msg4));
  const char *msg5 = "<h3>Number of Requests Since Server Started</h3><h4>";
  char *req = (char*)malloc(50);
  sprintf(req, "%d", num_req);
  write(socket, msg5, strlen(msg5));
  write(socket, req, strlen(req));
  write(socket, msg4, strlen(msg4));


}
extern "C" void killzombie(int sig)
{
  int pid = 1;
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

