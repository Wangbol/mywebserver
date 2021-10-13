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

// ͨ���ļ�����ȡ�ļ�������
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
			sprintf(to, "%%%02x", (int)*from & 0xff);      //ת��Ϊʮ������
			to += 3;
			len += 3;
		}
	}
	*to = '\0';
}

//ʮ������ת��Ϊʮ����
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
	//״̬��
	int s = sprintf(buf, "http/1.1 %d %s\r\n", no, desc);  // ��noΪ0������strlen(buf)�õ��ĳ���С��ʵ���ַ�������
	send(cfd, buf, s, 0);                                  //strlen����\0 �ͽ���
	//��Ϣ��ͷ--�ļ����ͼ�����
	sprintf(buf, "Content-Type:%s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length:%d\r\n", len);
	send(cfd, buf, strlen(buf), 0);
	//����
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
	//ƴһ��htmlҳ�棬��ʾ�ļ����е��ļ�
	char buf[4096] = "";
	sprintf(buf, "<html><head><title>Index of: %s</title><head>", dirname);
	sprintf(buf + strlen(buf), "<body><h1>Index of: %s</h1><table>", dirname);

	char path[1024] = "";
	char enStr[1024] = "";
	struct dirent** ptr;                                    // Ŀ¼�����ָ��
	int num = scandir(dirname, &ptr, NULL, alphasort);     //��ȡĿ¼���ļ����ļ�������ĸ����
	for (int i = 0; i < num; i++)
	{
		char* name = ptr[i]->d_name;
		//ƴ���ļ�����·�����Ա㹤�������ܹ��ҵ���Щ�ļ�
		sprintf(path, "%s/%s", dirname, name);
		//�жϸ�Ŀ¼�¸��ļ����ļ������ļ���
		struct stat st;
		int ret = stat(path, &st);
		if (ret == -1)
		{
			//404
			sendRespondHead(cfd, 404, "File Not Found", getFileType(".html"), -1);
			sendFile(cfd, "404.html");
			return;
		}
		//���͸���ҳʱ����Գ������е����Ľ��б���
		
		encodeStr(enStr, sizeof(enStr), name);
		//������ļ�
		if (S_ISREG(st.st_mode))
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", enStr, name, st.st_size);
		}
		//������ļ���
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
	//���������
	char method[12], path[1024], protocol[12];
	sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
                                              
	//ȥ��path�е� /
	char* file = path + 1;
	//��������������� %20&15 ��ʽ���ַ�������ת������
	decodeStr(file, file);
	//��û��ָ��������Դ����Ĭ��ΪĿ¼�ĵ�ǰλ��
	if (strcmp(path, "/") == 0)
	{
		file = "./";
	}
	//��ȡfile�����ԣ����ж����ļ��л����ļ�
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)              // �ļ�������
	{
		//404
		sendRespondHead(cfd, 404, "File Not Found", getFileType(".html"), -1);
		sendFile(cfd, "404.html");
		return;
	}
	if (S_ISDIR(st.st_mode))    // ��Ŀ¼
	{
		//������Ӧͷ
		sendRespondHead(cfd, 200, "OK", getFileType(".html"), -1);      //�ļ�����δ֪����-1����
		//����Ŀ¼
		sendDir(cfd, file);
	}
	else if (S_ISREG(st.st_mode))  // ���ļ�
	{
		//������Ӧͷ
		sendRespondHead(cfd, 200, "OK", getFileType(file), st.st_size);
		//�����ļ�
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
			if (recv(cfd, &c, 1, MSG_PEEK) > 0 && c == '\n')    //MSG_PEEK--�Կ�����ʽ��cfd�����������ݣ���0��ʾ��ȡ���ݺ��ɾ��cfd��������Ӧ����
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
	//��ȡ����
	char line[1024] = "";
	int len = getLine(cfd, line, sizeof(line));
	if (len == 0)
	{
		cout << "�ͻ��˹ر���..." << endl;
		disconnect(cfd,epfd);
		return;
	}
	else if (len == -1)
	{
		perror("read()");
		disconnect(cfd, epfd);
		return;
	}
	else     // ��������û����
	{
		cout << "���������ݣ�" << line << endl;
		while (len)            // ʣ�����ϢΪ������֮�����Ϣ
		{
			char buf[1024] = "";
			len = getLine(cfd, buf, sizeof(buf));
		}
	}
	//�ж��Ƿ�Ϊget����
	if (strncasecmp("get", line, 3) == 0)
	{
		//���� get ����
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
	int cfd = accept(lfd, (struct sockaddr*)&cliaddr, &len);   //�����Ӷ�����ȡ����һ����������
	//ѭ����ȡ����ʱ��Ӧ��cfd���óɷ�����ģʽ
	int flag = fcntl(cfd, F_GETFL);  //--��ȡcfd�ı�־λ
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	//inet_ntop--��������ת�ɵ��ʽ
	printf("new client ip=%s port=%d\n", inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, 16), ntohs(cliaddr.sin_port));

	//��cfd����
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;   // ���ش�����ʽ
	epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
}

int createListenFd(int port)
{
	//����socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
	{
		perror("socket()");
		return -1;
	}
	//��IP�Ͷ˿�
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
	//����
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
	signal(SIGPIPE, SIG_IGN);     //��ֹ�������ȡ���ļ�ʱ��ʹserver����SIGPIPE����

	//�޸Ľ��̹���Ŀ¼
	char pwdPath[256] = "";
	char* path = getenv("PWD");
	strcpy(pwdPath, path);
	strcat(pwdPath, "/resources");
	chdir(pwdPath);

	int lfd = createListenFd(8008);
	
	//����epoll��
	int epfd = epoll_create(1);  // ������ 1 Ϊ�����ļ��������ĸ����������Զ�����
	//��lfd����
	struct epoll_event ev, evs[1024];    //�ṹ������evs�������պ������ʱ���صı仯�ڵ��׵�ַ
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	ThreadPool threadpool(3, 10);   //ʵ����һ���̳߳�
	//while����
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
		else           // ���ļ������������仯
		{
			int i;
			for (i = 0; i < nread; i++)
			{
				//�ж�lfd�仯�������Ƕ��¼��仯,�����׽�������������--���µ����ӽ���
				if (evs[i].data.fd == lfd)
				{
					acceptConnect(lfd, epfd);
				}
				else if (evs[i].events & EPOLLIN)   //cfd�仯�����Ƕ��¼��仯
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

	// ����������ҡ�.���ַ�, �粻���ڷ���NULL
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
