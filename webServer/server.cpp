#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<dirent.h>
#include<arpa/inet.h>
#include<cstdio>
#include<ctype.h>
#include<dirent.h>
#include<errno.h>
#include<signal.h>
#include "threadPool.h"
#include<string.h>
#include<stdlib.h>
#include<sys/epoll.h>

using namespace std;

// 通过文件名获取文件的类型
const char* getFileType(char* name);

void encodeStr(char* to, int toSize, char* from)
{
	for (int len = 0; *from != '\0' && len + 4 < toSize; from++)
	{
		if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0)
		{
			*to = *from;
			to++;
			len++;
		}
		else
		{
			sprintf(to, "%%%02x", (int)*from & 0xff);      //转换为十六进制
			to += 3;
			len += 3;
		}
	}
	*to = '\0';
}

//十六进制转换为十进制
int hexit(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return 0;
}

void decodeStr(char* to, char* from)
{
	while (*from != '\0')
	{
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			*to = hexit(from[1]) * 16 + hexit(from[2]);
			from += 2;

		}
		else
		{
			*to = *from;
		}
		to++;
		from++;
	}
	*to = '\0';
	
}

void sendRespondHead(int& cfd, int no, const char* desc, const char* type, long int len)
{
	char buf[1024] = "";
	//状态行
	int s = sprintf(buf, "http/1.1 %d %s\r\n", no, desc);  // 若no为0，则用strlen(buf)得到的长度小于实际字符串长度
	send(cfd, buf, s, 0);                                  //strlen遇到\0 就结束
	//消息报头--文件类型及长度
	sprintf(buf, "Content-Type:%s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length:%d\r\n", len);
	send(cfd, buf, strlen(buf), 0);
	//空行
	send(cfd, "\r\n", 2, 0);

}

void sendFile(int& cfd, char* filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		perror("stat()");
		return;
	}
	char buf[4096] = "";
	int len = 0;
	while ((len = read(fd, buf, sizeof(buf))) > 0)
	{
		send(cfd, buf, len, 0);
	}
	if (len < 0)
	{
		perror("read()");
		return;
	}
	close(fd);
}

void sendDir(int& cfd, char* dirname)
{
	//拼一个html页面，显示文件夹中的文件
	char buf[4096] = "";
	sprintf(buf, "<html><head><title>Index of: %s</title><head>", dirname);
	sprintf(buf + strlen(buf), "<body><h1>Index of: %s</h1><table>", dirname);

	char path[1024] = "";
	char enStr[1024] = "";
	struct dirent** ptr;                                    // 目录项二级指针
	int num = scandir(dirname, &ptr, NULL, alphasort);     //读取目录下文件，文件名按字母排序
	for (int i = 0; i < num; i++)
	{
		char* name = ptr[i]->d_name;
		//拼接文件完整路径，以便工作进程能够找到这些文件
		sprintf(path, "%s/%s", dirname, name);
		//判断该目录下各文件是文件还是文件夹
		struct stat st;
		int ret = stat(path, &st);
		if (ret == -1)
		{
			//404
			sendRespondHead(cfd, 404, "File Not Found", getFileType(".html"), -1);
			sendFile(cfd, "404.html");
			return;
		}
		//发送给网页时，需对超链接中的中文进行编码
		
		encodeStr(enStr, sizeof(enStr), name);
		//如果是文件
		if (S_ISREG(st.st_mode))
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", enStr, name, st.st_size);
		}
		//如果是文件夹
		if (S_ISDIR(st.st_mode))
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>", enStr, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));

	}
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
}

void httpGetRequest(const char* request, int& cfd)
{
	//拆分请求行
	char method[12], path[1024], protocol[12];
	sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
                                              
	//去掉path中的 /
	char* file = path + 1;
	//浏览器发过来的是 %20&15 形式的字符串，需转成中文
	decodeStr(file, file);
	//若没有指定访问资源，则默认为目录的当前位置
	if (strcmp(path, "/") == 0)
	{
		file = "./";
	}
	//获取file的属性，以判断是文件夹还是文件
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)              // 文件不存在
	{
		//404
		sendRespondHead(cfd, 404, "File Not Found", getFileType(".html"), -1);
		sendFile(cfd, "404.html");
		return;
	}
	if (S_ISDIR(st.st_mode))    // 是目录
	{
		//发送响应头
		sendRespondHead(cfd, 200, "OK", getFileType(".html"), -1);      //文件长度未知可用-1代替
		//发送目录
		sendDir(cfd, file);
	}
	else if (S_ISREG(st.st_mode))  // 是文件
	{
		//发送响应头
		sendRespondHead(cfd, 200, "OK", getFileType(file), st.st_size);
		//发送文件
		sendFile(cfd, file);
	}
}

int getLine(int& cfd, char* buf, int size)
{
	char* tp = buf;
	char c;
	--size;
	while ((tp - buf) < size)
	{
		if (read(cfd, &c, 1) <= 0)
		{
			break;
		}
		if (c == '\r')
		{
			if (recv(cfd, &c, 1, MSG_PEEK) > 0 && c == '\n')    //MSG_PEEK--以拷贝方式从cfd缓冲区读数据；而0表示读取数据后会删除cfd缓冲区对应数据
			{
				read(cfd, &c, 1);
			}
			else
			{
				*tp++ = '\n';
			}
			break;
		}
		*tp++ = c;
	}
	*tp = '\0';
	return tp - buf;
}

void disconnect(int cfd, int epfd)
{
	int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
	if (ret == -1)
	{
		perror("epoll_ctl()");
		return;
	}
	close(cfd);
}

void readClientRequest(int* arg)
{
	int cfd = arg[0];
	int epfd = arg[1];
	//读取请求
	char line[1024] = "";
	int len = getLine(cfd, line, sizeof(line));
	if (len == 0)
	{
		cout << "客户端关闭了..." << endl;
		disconnect(cfd,epfd);
		return;
	}
	else if (len == -1)
	{
		perror("read()");
		disconnect(cfd, epfd);
		return;
	}
	else     // 还有数据没读完
	{
		cout << "请求行数据：" << line << endl;
		while (len)            // 剩余的信息为请求行之外的信息
		{
			char buf[1024] = "";
			len = getLine(cfd, buf, sizeof(buf));
		}
	}
	//判断是否为get请求
	if (strncasecmp("get", line, 3) == 0)
	{
		//处理 get 请求
		httpGetRequest(line, cfd);

		disconnect(cfd, epfd);
	}
}

void acceptConnect(int lfd, int epfd)
{
	struct sockaddr_in cliaddr;
	struct epoll_event ev;
	char ip[16] = "";
	socklen_t len = sizeof(cliaddr);
	int cfd = accept(lfd, (struct sockaddr*)&cliaddr, &len);   //从连接队列中取出第一个连接请求
	//循环读取数据时，应将cfd设置成非阻塞模式
	int flag = fcntl(cfd, F_GETFL);  //--获取cfd的标志位
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	//inet_ntop--将二进制转成点分式
	printf("new client ip=%s port=%d\n", inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, 16), ntohs(cliaddr.sin_port));

	//将cfd上树
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;   // 边沿触发方式
	epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
}

int createListenFd(int port)
{
	//创建socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
	{
		perror("socket()");
		return -1;
	}
	//绑定IP和端口
	struct sockaddr_in serv;
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	serv.sin_addr.s_addr = htonl(INADDR_ANY);
	int val;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	int ret = bind(lfd, (struct sockaddr*)&serv, sizeof(serv));
	if (ret < 0)
	{
		perror("bind()");
		return 0;
	}
	//监听
	ret = listen(lfd, 1024);
	if (ret < 0)
	{
		perror("listen()");
		return 0;
	}
	return lfd;
}

int main()
{
	signal(SIGPIPE, SIG_IGN);     //防止浏览器读取大文件时，使server发生SIGPIPE错误

	//修改进程工作目录
	char pwdPath[256] = "";
	char* path = getenv("PWD");
	strcpy(pwdPath, path);
	strcat(pwdPath, "/resources");
	chdir(pwdPath);

	int lfd = createListenFd(8008);
	
	//创建epoll树
	int epfd = epoll_create(1);  // 参数中 1 为监听文件描述符的个数，可以自动扩充
	//将lfd上树
	struct epoll_event ev, evs[1024];    //结构体数组evs用来接收后面监听时返回的变化节点首地址
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	ThreadPool threadpool(3, 10);   //实例化一个线程池
	//while监听
	while (1)
	{
		int nread = epoll_wait(epfd, evs, 1024, -1);
		if (nread < 0)
		{
			perror("epoll_wait()");
			break;
		}
		else if (nread == 0)
		{
			continue;
		}
		else           // 有文件描述符发生变化
		{
			int i;
			for (i = 0; i < nread; i++)
			{
				//判断lfd变化，并且是读事件变化,监听套接字描述符就绪--有新的连接接入
				if (evs[i].data.fd == lfd)
				{
					acceptConnect(lfd, epfd);
				}
				else if (evs[i].events & EPOLLIN)   //cfd变化，且是读事件变化
				{
					int* arg = new int[2];
					arg[0] = evs[i].data.fd;
					arg[1] = epfd;
					threadpool.addTask(Task(readClientRequest, arg));
					
				}
			}
		}
	}
}

const char* getFileType(char* name)
{
	const char* dot;

	// 自右向左查找‘.’字符, 如不存在返回NULL
	dot = strrchr(name, '.');
	if (dot == NULL)
	{
		return "text/plain; charset=utf-8";
	}
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
	{
		return "text/html; charset=utf-8";
	}
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
	{
		return "image/jpeg";
	}
	if (strcmp(dot, ".gif") == 0)
	{
		return "image/gif";
	}
	if (strcmp(dot, ".png") == 0)
	{
		return "image/png";
	}
	if (strcmp(dot, ".css") == 0)
	{
		return "text/css";
	}
	if (strcmp(dot, ".au") == 0)
	{
		return "audio/basic";
	}
	if (strcmp(dot, ".wav") == 0)
	{
		return "audio/wav";
	}
	if (strcmp(dot, ".avi") == 0)
	{
		return "video/x-msvideo";
	}
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
	{
		return "video/quicktime";
	}
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
	{
		return "video/mpeg";
	}
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
	{
		return "model/vrml";
	}
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
	{
		return "audio/midi";
	}
	if (strcmp(dot, ".mp3") == 0)
	{
		return "audio/mpeg";
	}
	if (strcmp(dot, ".ogg") == 0)
	{
		return "application/ogg";
	}
	if (strcmp(dot, ".pac") == 0)
	{
		return "application/x-ns-proxy-autoconfig";
	}
	return "text/plain; charset=utf-8";
}
