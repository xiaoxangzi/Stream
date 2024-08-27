#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
using namespace std;

struct OutStream{
    virtual void write(const char *__restrict s, size_t len) = 0;

    virtual ~OutStream() = default;

    virtual void flush() = 0;

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
    UnixFileOutStream(int fd_): fd(fd_){
    }

    void flush() override {
        
    }

    void write(const char *__restrict s, size_t len)override{
        this_thread::sleep_for(0.1s);
        ::write(fd, s, len);
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
    unique_ptr<char[]>buf; // c++14  unique_ptr对数组偏特化
    size_t top = 0;
    int mode; // _IONBF 无缓冲   _IOLBF 行缓冲   _IOFBF全缓冲

public:
    explicit BufferedOutStream(unique_ptr<OutStream> out_,int mode_ = FullBuf)  // explicit 放置隐式转换
        : out(move(out_))
        , mode(mode_){  
        if (mode != NoBuf){
            buf = make_unique<char[]>(BUFSIZ);
        }
    }

    

    void flush() override{
        out->write(buf.get(),top);
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
    }
};

BufferedOutStream mout(make_unique<UnixFileOutStream>(STDOUT_FILENO), BufferedOutStream::LineBuf);
BufferedOutStream merr(make_unique<UnixFileOutStream>(STDERR_FILENO), BufferedOutStream::NoBuf);


int main(){

    mout.puts("Hello,");
    mout.puts("World\n");


    merr.puts("Hello,");
    merr.puts("World\n");



    return 0;
}