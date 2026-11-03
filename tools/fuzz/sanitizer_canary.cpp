#include <limits>
#include <string_view>
int main(int argc,char **argv){if(argc<2)return 2;if(std::string_view(argv[1])=="asan"){volatile int *p=new int[4];volatile int i=4;p[i]=42;return p[i];}volatile int x=std::numeric_limits<int>::max();return x+1;}
