#include <asm-generic/errno-base.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <cerrno>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
using namespace std;

struct InStream{ // 输入流是操作系统在管理

    virtual size_t read(char *__restrict s, size_t len) = 0;
    virtual ~InStream() = default;

    virtual int getchar(){
        char c;
        size_t n = read(&c, 1);
        if (n == 0){
            return EOF;
        }
        return c; // 调用的是继承后写的write
    }

    string getline(){
        string str;
        while (true) {
            int c = getchar();
            if (c ==EOF){   //EOF 是什么？
                break;
            }   
            if (c =='\n'){
                break;
            }
            str.push_back(c);
        }
        return str;
    }

};

struct BufferedInStream :InStream{
private:
    unique_ptr<InStream> in;
    char *buf; 
    size_t top = 0; // 目前读到的位置
    size_t max = 0; // 最大可读取的值

    bool refill(){
        top = 0;
        max = in->read(buf, BUFSIZ); // max <= BUFSIZ    一次性读完一个BUFSIZ
        // 如果用户只输入  hello\n   后面全是 0000000000数据     ，将其全部读出，但系统会根据真实数据返回max  及max=6
        // stdin  自带行缓冲，因此每次会读入一行     max是一行的数据大小 
        return max !=0;
    }

public:
    explicit BufferedInStream(unique_ptr<InStream> in_)  // explicit 放置隐式转换
        : in(move(in_))
    {  
        buf = (char *)valloc(BUFSIZ); // valloc 保证总是对齐到页面大小
    }

    int getchar()override{
        if (top == max)  // 使用缓冲区，就是避免重复的调用系统的io
            if (!refill())
                return EOF;
        return buf[top++];
    }

    size_t read(char *__restrict s, size_t len) override {   // 对底层接口进行重写
        size_t i;
        for (i = 0;i != len; i++){
            if (top == max){
                if (!refill())
                    break;
            }
            int c = buf[top++];
            s[i] = c;
        }

        return i;
        
    }

    BufferedInStream(BufferedInStream && ) = delete; // 定义析构函数需要把移动赋值函数删除

    ~BufferedInStream(){
        free(buf);
    }
};

struct UnixFileInStream:InStream{
private:
    int fd;

public:
    explicit UnixFileInStream(int fd_): fd(fd_){
    }

    size_t read(char *__restrict s, size_t len) override {
        if (len == 0) return 0;
        std::this_thread::sleep_for(0.2s);
        ssize_t n = ::read(fd, s, len);
        if (n < 0)
            throw std::system_error(errno, std::generic_category());
        return n;
    }

    UnixFileInStream(UnixFileInStream &&) =delete;

    ~UnixFileInStream(){
        ::close(fd);
    }

};


struct OutStream{
    virtual void write(const char *__restrict s, size_t len) = 0;

    virtual ~OutStream() = default;

    virtual void flush() {
    }

    void puts(const char *__restrict s){
        write(s, strlen(s)); // 调用的是继承后写的write
    }
    virtual void putchar(char c){
        write(&c,1);
    }
};


struct UnixFileOutStream:OutStream{
private:
    int fd;

public:
    explicit UnixFileOutStream(int fd_): fd(fd_){
    }

    void write(const char *__restrict s, size_t len)override{
        if (len == 0) return;
        this_thread::sleep_for(0.2s);
        ssize_t written = ::write(fd, s, len);
        if (written < 0){
            throw system_error(errno, generic_category());
        }
        if (written == 0)
            throw system_error(EPIPE, generic_category()); // 文件对面已经关闭
        while (written != len) {
            written = ::write(fd, s, len);
            if (written < 0)
                throw system_error(errno, generic_category());
            if (written == 0)
                throw system_error(EPIPE, generic_category());
            s += written;
            len -= written;
        }
    }
    UnixFileOutStream(UnixFileOutStream &&) =delete;

    ~UnixFileOutStream(){
        ::close(fd);
    }

};


struct BufferedOutStream :OutStream{
    enum BuffMode{
        FullBuf,
        LineBuf,
        NoBuf,
    };
private:
    unique_ptr<OutStream> out;
    char *buf; // c++14  unique_ptr对数组偏特化
    size_t top = 0;
    int mode; // _IONBF 无缓冲   _IOLBF 行缓冲   _IOFBF全缓冲

public:
    explicit BufferedOutStream(unique_ptr<OutStream> out_,int mode_ = FullBuf)  // explicit 放置隐式转换
        : out(move(out_))
        , mode(mode_)
    {  
        if (mode != NoBuf){
            buf = (char *)valloc(BUFSIZ); // valloc 保证总是对齐到页面大小
        }
    }

    

    void flush() override{
        out->write(buf,top);
        top = 0;
    }

    void putchar(char c) override {   // 对底层接口进行重写
        if (mode == NoBuf) {// 无缓冲 直接输出
            out->write(&c, 1);
            return;
        }
        if (top==BUFSIZ) // 全缓冲
            flush();
        char a = c;
        buf[top++] = c;
        if ((mode == LineBuf && c == '\n')) flush();  // 行缓冲
    }

    void write(const char *__restrict s, size_t len) override{  // 可以
        if (mode == NoBuf){
            out->write(s, len);
            return;
        }
        
        for(const char * __restrict p = s; p != s+len;++p){
            if (top==BUFSIZ) 
                flush();
            char c = *p;
            buf[top++] = c;
            
            if (mode == LineBuf && c == '\n') 
                flush();  // 行缓冲
        }
    }

    void close(){
        flush();
    }

    BufferedOutStream(BufferedOutStream && ) = delete; // 定义析构函数需要把移动赋值函数删除

    ~BufferedOutStream(){
        close();
        free(buf);
    }
};
BufferedInStream myin(make_unique<UnixFileInStream>(STDIN_FILENO));

BufferedOutStream mout(make_unique<UnixFileOutStream>(STDOUT_FILENO), BufferedOutStream::LineBuf);
BufferedOutStream merr(make_unique<UnixFileOutStream>(STDERR_FILENO), BufferedOutStream::NoBuf);

void mperror(const char *mag) {
    merr.puts(mag);
    merr.puts(":");
    merr.puts(strerror(errno));  // 将系统错误码转换为文字
    merr.puts("\n");
}


enum class OpenFlag{
    Read,
    Write,
    Append,
    ReadWrite,
};

map<OpenFlag, int> openFlagToUnixFlag = {
    {OpenFlag::Read, O_RDONLY},
    {OpenFlag::Write, O_WRONLY | O_TRUNC | O_CREAT},
    {OpenFlag::Append, O_WRONLY | O_APPEND | O_CREAT},
    {OpenFlag::ReadWrite, O_RDWR | O_CREAT},
};

unique_ptr<OutStream> out_file_open(const char * path, OpenFlag flag){
    int oflag = openFlagToUnixFlag.at(flag);
    // oflag |= O_DIRECT;
    int fd = open(path, oflag);
    if (fd < 0) {
        // 第一种方法
        // mperror(path); // 封装好的函数   系统的cat命令调用的就是perror函数  mperror 为我实现的版本
        // fprintf(stderr, "msg:%s\n", strerror(errno));

        // 第二种方法
        throw system_error(errno, generic_category());
    }
    auto file= make_unique<UnixFileOutStream>(fd);

    return file; // 类型转换实现多态
}
std::unique_ptr<InStream> in_file_open(const char *path, OpenFlag flag) {
    int oflag = openFlagToUnixFlag.at(flag);
    int fd = open(path, oflag);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category());
    }
    auto file = std::make_unique<UnixFileInStream>(fd);
    return file;
    // return std::make_unique<BufferedInStream>(std::move(file));
}




int main(){
    char c = myin.getchar();
    printf("%d\n",c);
    
    
    // {
    //     auto p = out_file_open("/tmp/b.txt", OpenFlag::Write); // 只读文件read就会打开失败
    //     p->puts("hello! world\n");
    // }
    // {
    //     auto p = in_file_open("/tmp/b.txt", OpenFlag::Read); // 只读文件read就会打开失败
    //     auto c = p->getline();  // 文件默认完全缓冲    Write 只能创建文件然后写入
    //     printf("%s\n", c.c_str());
    // }
    
    return 0;
}