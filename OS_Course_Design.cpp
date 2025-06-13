#include <stdio.h>
#include <fcntl.h>
#include <string>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <cstring>
#include <random>
#include <algorithm>
#include <string.h>
#include <cstdlib>
using namespace std;

// I-node 地址数量
const unsigned int NADDR = 11;

// Block 大小
const unsigned short BLOCK_SIZE = 1024;

// 最大文件大小
const unsigned int FILE_SIZE_MAX = ((NADDR - 1) + BLOCK_SIZE / sizeof(int)) * BLOCK_SIZE;
//                                 ((11 - 1) + 1024/4) * 1024 = 266 * 1024

// Block 数量
const unsigned short BLOCK_NUM = 16294;

// I-node 大小
const unsigned short INODE_SIZE = 128;

// I-node 数量
const unsigned short INODE_NUM = 256;

/*

+-------------------------------------------------------------------------+
| blank block | Super Block | inode bitmap |     inodes     | Data Blocks |
|    1KB      |     1KB     |     1KB      |      32KB      |   16349KB   |
+-------------------------------------------------------------------------+
总共16 * 1024 = 16384个Block

DataBlocks:
+--------------------------------------------------------------------------------+
|   Block0  |    ...    |   Block299  |  stack |  ...   |  Block16347  |  stack  |
+--------------------------------------------------------------------------------+

可用于数据的Block总数为16294（16349 - 16349 / 300 - 1）
*/

// inode的起始位置
const unsigned int INODE_START = 3 * BLOCK_SIZE;

// 数据起始位置
const unsigned int DATA_START = INODE_START + INODE_SIZE * INODE_NUM;

// 子目录的最大数量
const unsigned int DIRECTORY_NUM = 16;

// 文件名的最大长度
const unsigned short FILE_NAME_LENGTH = 14;

// 系统文件权限
const unsigned short SYSTEM_PERMISSION = 2;

// 用户文件权限
const unsigned short USER_PERMISSION = 3;

// 权限
const unsigned short OWN_W = 1;
const unsigned short OWN_R = 1 << 1;

// inode设计
struct inode
{
    unsigned int i_ino;           //inode号.
    unsigned int di_addr[NADDR];  //Number of data blocks where the file stored.
    unsigned short di_number;     //Number of associated files.
    unsigned short di_mode;       //文件类型.
    unsigned short icount;        //连接数
    unsigned short permission;    //文件权限
    unsigned int di_size;         //文件大小
    char time[68];
};

// 超级块设置
struct filsys
{
    unsigned short s_num_inode;   //inode总数
    unsigned short s_num_finode;  //空闲inode数.
    unsigned short s_size_inode;  //inode大小.
    unsigned short s_num_block;   //block的数量.
    unsigned short s_num_fblock;  //空闲块的数量.
    unsigned short s_size_block;  //block的大小.
    unsigned short special_stack[300];
    int special_free;
};

// 目录设计
struct directory
{
    char fileName[20][FILE_NAME_LENGTH];    //目录名称
    unsigned int inodeID[DIRECTORY_NUM];    //inode号
};

// 功能函数声明
void CommParser(inode*&);

// 全局变量
FILE* fd = NULL;  // 文件系统位置

// 超级块
filsys superBlock;

// 1代表已经使用，0表示空闲
unsigned short inode_bitmap[INODE_NUM];

// 当前目录
directory currentDirectory;

char ab_dir[100][14];

unsigned short dir_pointer;

const char* filesystem = "scut.os";  // /文件系统名称

// 分配空闲 Block
void find_free_block(unsigned int& inode_number){
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fread(&superBlock, sizeof(filsys), 1, fd);
    if (superBlock.special_free == 0)
    {
        if (superBlock.special_stack[0] == 0)
        {
            printf("\033[31mNo value block!\033[0m\n");
            return;
        }
        unsigned short stack[301];

        for (int i = 0; i < 300; i++)
        {
            stack[i] = superBlock.special_stack[i];
        }
        stack[300] = superBlock.special_free;
        fseek(fd, DATA_START + (superBlock.special_stack[0] - 300) * BLOCK_SIZE, SEEK_SET);
        fwrite(stack, sizeof(stack), 1, fd);

        fseek(fd, DATA_START + superBlock.special_stack[0] * BLOCK_SIZE, SEEK_SET);
        fread(stack, sizeof(stack), 1, fd);
        for (int i = 0; i < 300; i++)
        {
            superBlock.special_stack[i] = stack[i];
        }
        superBlock.special_free = stack[300];
    }
    inode_number = superBlock.special_stack[superBlock.special_free];
    superBlock.special_free--;
    superBlock.s_num_fblock--;
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(filsys), 1, fd);
}

// 回收 Block
void recycle_block(unsigned int& inode_number){
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fread(&superBlock, sizeof(filsys), 1, fd);
    if (superBlock.special_free == 299)
    {
        unsigned int block_num;
        unsigned short stack[301];
        if (superBlock.special_stack[0] == 0)
            block_num = 16119;
        else
            block_num = superBlock.special_stack[0] - 300;
        for (int i = 0; i < 300; i++)
        {
            stack[i] = superBlock.special_stack[i];
        }
        stack[300] = superBlock.special_free;
        fseek(fd, DATA_START + block_num * BLOCK_SIZE, SEEK_SET);
        fwrite(stack, sizeof(stack), 1, fd);
        block_num -= 300;
        fseek(fd, DATA_START + block_num * BLOCK_SIZE, SEEK_SET);
        fread(stack, sizeof(stack), 1, fd);
        for (int i = 0; i < 300; i++)
        {
            superBlock.special_stack[i] = stack[i];
        }
        superBlock.special_free = stack[300];
    }
    superBlock.special_free++;
    superBlock.s_num_fblock++;
    superBlock.special_stack[superBlock.special_free] = static_cast<unsigned short>(inode_number);
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(filsys), 1, fd);

}

// 系统初始化
bool Format(){
    // 在当前目录新建一个文件作为文件卷
    FILE* fd = fopen(filesystem, "wb+");
    if (fd == NULL)
    {
        printf("\033[31m[Initialization Error] The file volume is unavailable!\033[0m\n");
        return false;
    }

    filsys superBlock;
    superBlock.s_num_inode = INODE_NUM;            // inode总数
    superBlock.s_num_block = BLOCK_NUM + 90;       // + 1 + 1 + 1 + 32 + 55
    superBlock.s_size_inode = INODE_SIZE;          // inode大小
    superBlock.s_size_block = BLOCK_SIZE;          // block大小
    superBlock.s_num_fblock = BLOCK_NUM - 1;       // 空闲块数量
    superBlock.s_num_finode = INODE_NUM - 2;       // 空闲inode数量



    superBlock.special_stack[0] = 599;
    for (int i = 1; i < 300; i++)
    {
        superBlock.special_stack[i] = 299 - i;
    }
    superBlock.special_free = 297;  // 初始时，空闲块为297个
    // 写入超级快
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(filsys), 1, fd);
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fread(&superBlock, sizeof(filsys), 1, fd);

    // 初始化位示图
    unsigned short inode_bitmap[INODE_NUM];

    memset(inode_bitmap, 0, INODE_NUM);
    inode_bitmap[0] = 1;
    inode_bitmap[1] = 1;
    // 写入位图
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(unsigned short) * INODE_NUM, 1, fd);

    // 成组连接
    unsigned short stack[301];
    for (int i = 0; i < BLOCK_NUM / 300; i++)
    {
        memset(stack, 0, sizeof(stack));
        for (unsigned int j = 0; j < 300; j++)
        {
            stack[j] = (299 + i * 300) - j;
        }
        stack[0] = 299 + (i + 1) * 300;
        stack[300] = 299;
        fseek(fd, DATA_START + (299 + i * 300) * BLOCK_SIZE, SEEK_SET);
        fwrite(stack, sizeof(unsigned short) * 301, 1, fd);
    }

    memset(stack, 0, sizeof(stack));
    for (int i = 0; i < 149; ++i)
    {
        stack[i] = 16348 - i;
    }

    stack[0] = 0;
    stack[300] = 148;
    fseek(fd, DATA_START + 16349 * BLOCK_SIZE - sizeof(unsigned short) * 301, SEEK_SET);
    fwrite(stack, sizeof(unsigned short) * 301, 1, fd);

    // 创建根目录
    inode iroot_tmp;
    iroot_tmp.i_ino = 0;
    iroot_tmp.di_number = 2;
    iroot_tmp.di_mode = 0;
    iroot_tmp.di_size = 0;
    memset(iroot_tmp.di_addr, -1, sizeof(unsigned int) * NADDR);
    iroot_tmp.di_addr[0] = 0;
    iroot_tmp.permission = USER_PERMISSION;
    iroot_tmp.icount = 0;
    time_t t = time(0);
    strftime(iroot_tmp.time, sizeof(iroot_tmp.time), "%Y/%m/%d %X %A %jday %z", localtime(&t));
    iroot_tmp.time[64] = 0;
    fseek(fd, INODE_START, SEEK_SET);
    fwrite(&iroot_tmp, sizeof(inode), 1, fd);

    // 直接创建文件
    directory droot_tmp;
    memset(droot_tmp.fileName, 0, sizeof(char) * DIRECTORY_NUM * FILE_NAME_LENGTH);
    memset(droot_tmp.inodeID, -1, sizeof(unsigned int) * DIRECTORY_NUM);
    strcpy(droot_tmp.fileName[0], ".");
    droot_tmp.inodeID[0] = 0;
    strcpy(droot_tmp.fileName[1], "..");
    droot_tmp.inodeID[1] = 0;
    //系统文件
    strcpy(droot_tmp.fileName[2], "system");
    droot_tmp.inodeID[2] = 1;

    // 写入
    fseek(fd, DATA_START, SEEK_SET);
    fwrite(&droot_tmp, sizeof(directory), 1, fd);

    // 创建系统文件
    const char* system_content = "This is a system file. It contains system information and configurations.\n";
    inode iaccouting_tmp;
    iaccouting_tmp.i_ino = 1;
    iaccouting_tmp.di_number = 1;
    iaccouting_tmp.di_mode = 1;
    iaccouting_tmp.di_size = strlen(system_content);
    memset(iaccouting_tmp.di_addr, -1, sizeof(unsigned int) * NADDR);
    iaccouting_tmp.di_addr[0] = 1;
    iaccouting_tmp.permission = SYSTEM_PERMISSION;
    iaccouting_tmp.icount = 0;
    t = time(0);
    strftime(iaccouting_tmp.time, sizeof(iaccouting_tmp.time), "%Y/%m/%d %X %A %jday %z", localtime(&t));
    iaccouting_tmp.time[64] = 0;
    fseek(fd, INODE_START + INODE_SIZE, SEEK_SET);
    fwrite(&iaccouting_tmp, sizeof(inode), 1, fd);


    fseek(fd, DATA_START + BLOCK_SIZE, SEEK_SET);
    fwrite(system_content, sizeof(char), strlen(system_content), fd);

    // 关闭文件
    fclose(fd);

    return true;
};

// 读取系统保存文件
bool Mount(){
    // 打开文件卷
    fd = fopen(filesystem, "rb+");
    if (fd == NULL)
    {
        printf("\033[31m[Initialization Error] The file volume is unavailable!\033[0m\n");
        return false;
    }

    // 读取超级块信息
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fread(&superBlock, sizeof(superBlock), 1, fd);

    // 读取节点映射表
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, sizeof(unsigned short) * INODE_NUM, 1, fd);

    // 读取当前目录
    fseek(fd, DATA_START, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);

    return true;
};

// 生成随机英文字符
string generateRandomContent(int totalSize) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static default_random_engine rng(time(0));
    static uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    string result;
    result.reserve(totalSize);
    for (int i = 0; i < totalSize; ++i)
        result += charset[dist(rng)];
    return result;
}

// 处理相对路径与绝对路径
bool ResolvePath(const string& path, string& filename, directory& targetDir, int& dirInodeID) {
    if (path.empty()) return false;
    if (count(path.begin(), path.end(), '/') == 1) {

        filename = path.substr(1);
        fseek(fd, DATA_START, SEEK_SET);
        fread(&targetDir, sizeof(directory), 1, fd);
        dirInodeID = 0;
    }
    else if (path[0] == '/') {
        size_t lastSlash = path.find_last_of('/');

        if (lastSlash == string::npos || lastSlash == path.size() - 1) {
            printf("\033[31m[File Error] Path Error!\033[0m\n");
            return false;
        }
        filename = path.substr(lastSlash + 1);
        string dirPath = path.substr(1, lastSlash - 1);

        vector<string> components;
        size_t pos = 0, next;
        while ((next = dirPath.find('/', pos)) != string::npos) {
            components.push_back(dirPath.substr(pos, next - pos));
            pos = next + 1;
        }
        if (pos < dirPath.size()) components.push_back(dirPath.substr(pos));
        inode dirInode;
        fseek(fd, DATA_START, SEEK_SET);
        fread(&targetDir, sizeof(directory), 1, fd);

        for (const string& dirName : components) {
            bool found = false;
            for (int i = 0; i < DIRECTORY_NUM; ++i) {
                if (strcmp(targetDir.fileName[i], dirName.c_str()) == 0) {
                    dirInodeID = targetDir.inodeID[i];
                    fseek(fd, INODE_START + dirInodeID * INODE_SIZE, SEEK_SET);
                    fread(&dirInode, sizeof(inode), 1, fd);
                    fseek(fd, DATA_START + dirInode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
                    fread(&targetDir, sizeof(directory), 1, fd);
                    found = true;
                    break;
                }
            }
            dirInodeID = targetDir.inodeID[0];
            if (!found || dirInode.di_mode != 0) {
                printf("\033[31m[Directory Error] The directory is unavaliable!\033[0m\n");
                return false;
            }
        }
    }
    else {
        filename = path;
        targetDir = currentDirectory;
        dirInodeID = currentDirectory.inodeID[0];
    }
    return true;
}

// 创建文件
bool CreateFile(const string& path, int blockCount) {
    // 检查传入参数和文件系统空间的有效性
    if (blockCount <= 0) {
        printf("\033[31m[Space Error] The block amount must be positive integer number!\033[0m\n");
        return false;
    }
    // 检查空闲块是否足够（此处多分配的一个用于可能的间接块）
    if (blockCount + 1 >= superBlock.s_num_fblock) {
        printf("\033[31m[Space Error] Insufficient space to create file!\033[0m\n");
        return false;
    }
    if (blockCount * BLOCK_SIZE > FILE_SIZE_MAX) {
        printf("\033[31m[Space Error] The requested file size exceeds the system's maximum file size limit!\033[0m\n");
        return false;
    }
    if (superBlock.s_num_finode <= 0) {
        printf("\033[31m[Space Error] No free i-node!\033[0m\n");
        return false;
    }

	string filename;
	directory targetDir;
	int dirInodeID;
    if (!ResolvePath(path, filename, targetDir, dirInodeID)) { 
        return false; 
    }

    // 检查文件是否已存在
    for (int i = 0; i < DIRECTORY_NUM; ++i) {
        if (strcmp(targetDir.fileName[i], filename.c_str()) == 0) {
            printf("\033[31m[File Error] The file '%s' is existed!\033[0m\n", filename.c_str());
            return false;
        }
    }

    // 检查目录是否已满，并预留位置
    int dir_entry_slot = -1;
    for (int i = 2; i < DIRECTORY_NUM; ++i) {
        if (strlen(targetDir.fileName[i]) == 0) {
            dir_entry_slot = i;
            break;
        }
    }

    if (dir_entry_slot == -1) {
        printf("\033[31m[Space Error] The target directory is full, unable to create new files!\033[0m\n");
        return false;
    }

	// 查找空闲 inode
	int new_ino = -1;
	for (int i = 0; i < INODE_NUM; i++) {
		if (inode_bitmap[i] == 0) {
			new_ino = i;
			break;
		}
	}
	if (new_ino == -1) {
        printf("\033[31m[System Error] No available inodes (inconsistent bitmap and superblock information)!\033[0m\n");
		return false;
	}

	// 填写 inode 信息
	inode newInode{};
	newInode.i_ino = new_ino;
	newInode.di_mode = 1;
	newInode.permission = USER_PERMISSION;
	newInode.icount = 0;
	newInode.di_number = 1;
	newInode.di_size = blockCount * BLOCK_SIZE;
	memset(newInode.di_addr, -1, sizeof(newInode.di_addr));

	// 分配 block，并写入随机数据
	unsigned int indirectBlk = -1;
    unsigned int indirectList_buildin[256];
    memset(indirectList_buildin, 0, sizeof(indirectList_buildin));
    int num = 0;

	for (int i = 0; i < blockCount; ++i) {
		unsigned int blk;
		find_free_block(blk);
        string content = generateRandomContent(BLOCK_SIZE);
        const char* data = content.c_str();
		fseek(fd, DATA_START + blk * BLOCK_SIZE, SEEK_SET);
		fwrite(data, BLOCK_SIZE, 1, fd);

		if (i < NADDR - 1) {
			newInode.di_addr[i] = blk;
		}
		else {
			if (indirectBlk == -1) {
				find_free_block(indirectBlk);
				newInode.di_addr[NADDR - 1] = indirectBlk;
			}
            indirectList_buildin[num++] = blk;
		}
	}

    if(indirectBlk != -1) {
        fseek(fd, DATA_START + indirectBlk * BLOCK_SIZE, SEEK_SET);
        fwrite(indirectList_buildin, sizeof(unsigned int), 256, fd);
    }

	time_t t = time(0);
	strftime(newInode.time, sizeof(newInode.time), "%Y/%m/%d %X %A %jday", localtime(&t));

	// 写入 inode
	fseek(fd, INODE_START + new_ino * INODE_SIZE, SEEK_SET);
	fwrite(&newInode, sizeof(inode), 1, fd);

	// 更新目标目录
    strcpy(targetDir.fileName[dir_entry_slot], filename.c_str());
    targetDir.inodeID[dir_entry_slot] = new_ino;

    inode parentInode;
    fseek(fd, INODE_START + dirInodeID * INODE_SIZE, SEEK_SET);
    fread(&parentInode, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + parentInode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(&targetDir, sizeof(directory), 1, fd);

    // 更新映射表
    inode_bitmap[new_ino] = 1;
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(inode_bitmap), 1, fd);

	// 更新超级块
	superBlock.s_num_finode--;
	fseek(fd, BLOCK_SIZE, SEEK_SET);
	fwrite(&superBlock, sizeof(superBlock), 1, fd);

    inode temp;
    fseek(fd, INODE_START + currentDirectory.inodeID[0] * INODE_SIZE, SEEK_SET);
    fread(&temp, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + temp.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);

    printf("\033[32m[Info] Successfully created file: %s, occupying %d blocks!\033[0m\n", filename.c_str(), blockCount);
	return true;
}

// 删除文件
bool DeleteFile(const string& path) {
    string filename;
    directory targetDir;
    int dirInodeID;
    if (!ResolvePath(path, filename, targetDir, dirInodeID)) {
        return false;
    }

    
    int fileIndex = -1;
    for (int i = 2; i < DIRECTORY_NUM; ++i) {
        if (strcmp(targetDir.fileName[i], filename.c_str()) == 0) {
            fileIndex = i;
            break;
        }
    }

    if (fileIndex == -1) {
        printf("\033[31m[File Error] Could not find the file!\033[0m\n");
        return false;
    }

    int inodeID = targetDir.inodeID[fileIndex];
    inode fileInode;
    fseek(fd, INODE_START + inodeID * INODE_SIZE, SEEK_SET);
    fread(&fileInode, sizeof(inode), 1, fd);
    if (fileInode.di_mode != 1) {
        printf("\033[31m[File Error] This is not a file!\033[0m\n");
        return false;
    }

    // 回收直接 block
    for (int i = 0; i < NADDR - 1; ++i) {
        if (fileInode.di_addr[i] != -1)
            recycle_block(fileInode.di_addr[i]);
    }

    // 回收间接 block
    if (fileInode.di_addr[NADDR - 1] != -1) {
        unsigned int indirectAddrs[BLOCK_SIZE / sizeof(unsigned int)];
        fseek(fd, DATA_START + fileInode.di_addr[NADDR - 1] * BLOCK_SIZE, SEEK_SET);
        fread(indirectAddrs, sizeof(indirectAddrs), 1, fd);
        for (int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); ++i) {
            if (indirectAddrs[i] != 0)
                recycle_block(indirectAddrs[i]);
        }
        recycle_block(fileInode.di_addr[NADDR - 1]);
    }

    // 回收 inode
    inode empty = {};
    fseek(fd, INODE_START + inodeID * INODE_SIZE, SEEK_SET);
    fwrite(&empty, sizeof(inode), 1, fd);

    // 更新 inode bitmap
    inode_bitmap[inodeID] = 0;
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(inode_bitmap), 1, fd);

    // 更新目录
    strcpy(targetDir.fileName[fileIndex], ""); 
    targetDir.inodeID[fileIndex] = -1;
    inode targetInode;
    fseek(fd, INODE_START + dirInodeID * INODE_SIZE, SEEK_SET); 
    fread(&targetInode, sizeof(inode), 1, fd);
    // 更新目录内容
    fseek(fd, DATA_START + targetInode.di_addr[0] * BLOCK_SIZE, SEEK_SET); 
    fwrite(&targetDir, sizeof(directory), 1, fd);

    // 更新超级快
    superBlock.s_num_finode++;
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(superBlock), 1, fd);

    inode temp;
    fseek(fd, INODE_START + currentDirectory.inodeID[0] * INODE_SIZE, SEEK_SET);
    fread(&temp, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + temp.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);
    printf("\033[32m[Info] Successfully delete file: %s !\033[0m\n", filename.c_str());
    return true;
}

// 文件复制
bool Copy(const string& srcPath, const string& destPath) {
    string srcName;
    directory srcDir;
    int srcDirInodeID;
    if (!ResolvePath(srcPath, srcName, srcDir, srcDirInodeID)) return false;

    // 找到源文件 inode
    int srcIdx = -1;
    for (int i = 0; i < DIRECTORY_NUM; ++i) {
        if (strcmp(srcDir.fileName[i], srcName.c_str()) == 0) {
            srcIdx = i;
            break;
        }
    }
    if (srcIdx == -1) {
        printf("\033[31m[File Error] Could not find the source file!\033[0m\n");
        return false;
    }

    int srcInodeID = srcDir.inodeID[srcIdx];
    inode srcInode;
    fseek(fd, INODE_START + srcInodeID * INODE_SIZE, SEEK_SET);
    fread(&srcInode, sizeof(inode), 1, fd);

    if (srcInode.di_mode != 1) {
        printf("\033[31m[File Error] The source path is not a file, could not be copied!\033[0m\n");
        return false;
    }

    // 读取源文件内容
    int blocks = (srcInode.di_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    string content;
    char buffer[BLOCK_SIZE];

    for (int i = 0; i < NADDR - 1 && i < blocks; ++i) {
        if (srcInode.di_addr[i] == -1) break;
        fseek(fd, DATA_START + srcInode.di_addr[i] * BLOCK_SIZE, SEEK_SET);
        fread(buffer, 1, BLOCK_SIZE, fd);
        content.append(buffer, BLOCK_SIZE);
    }

    if (blocks > NADDR - 1 && srcInode.di_addr[NADDR - 1] != -1) {
        unsigned int indirectBlk[BLOCK_SIZE / sizeof(unsigned int)];
        fseek(fd, DATA_START + srcInode.di_addr[NADDR - 1] * BLOCK_SIZE, SEEK_SET);
        fread(indirectBlk, sizeof(indirectBlk), 1, fd);
        for (int i = 0; i < blocks - (NADDR - 1); ++i) {
            fseek(fd, DATA_START + indirectBlk[i] * BLOCK_SIZE, SEEK_SET);
            fread(buffer, 1, BLOCK_SIZE, fd);
            content.append(buffer, BLOCK_SIZE);
        }
    }

    // 解析目标路径
    string destName;
    directory destDir;
    int destDirInodeID;
    if (!ResolvePath(destPath, destName, destDir, destDirInodeID)) return false;

    // 检查同名文件是否存在
    for (int i = 0; i < DIRECTORY_NUM; ++i) {
        if (strcmp(destDir.fileName[i], destName.c_str()) == 0) {
            printf("\033[31m[File Error] The target path already has a file with the same name!\033[0m\n");
            return false;
        }
    }

    // 检查目录是否已满，并预留位置
    int dir_entry_slot = -1;
    for (int i = 2; i < DIRECTORY_NUM; ++i) {
        if (strlen(destDir.fileName[i]) == 0) {
            dir_entry_slot = i;
            break;
        }
    }

    if (dir_entry_slot == -1) {
        printf("\033[31m[File Error] The target directory is full, unable to create new files!\033[0m\n");
        return false;
    }

    // 查找可用 inode
    int newIno = -1;
    for (int i = 0; i < INODE_NUM; ++i) {
        if (inode_bitmap[i] == 0) {
            newIno = i;
            break;
        }
    }
    if (newIno == -1) {
        printf("\033[31m[Space Error] There is no free i-node!\033[0m\n");
        return false;
    }

    // 初始化新 inode
    inode newInode{};
    newInode.i_ino = newIno;
    newInode.di_mode = 1;
    newInode.di_size = content.size();
    newInode.permission = USER_PERMISSION;
    newInode.di_number = 1;
    memset(newInode.di_addr, -1, sizeof(newInode.di_addr));

    const char* data = content.c_str();
    int totalBlocks = (content.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned int indirectBlk = -1;
    unsigned int indirectBlocks[256];
    memset(indirectBlocks, 0, sizeof(indirectBlocks));
    int num = 0;

    for (int i = 0; i < totalBlocks; ++i) {
        unsigned int blk;
        find_free_block(blk);
        fseek(fd, DATA_START + blk * BLOCK_SIZE, SEEK_SET);
        fwrite(data + i * BLOCK_SIZE, min((int)BLOCK_SIZE, (int)content.size() - i * BLOCK_SIZE), 1, fd);
        if (i < NADDR - 1) {
            newInode.di_addr[i] = blk;
        }
        else {
            if (indirectBlk == -1) {
                find_free_block(indirectBlk);
                newInode.di_addr[NADDR - 1] = indirectBlk;
            }
            indirectBlocks[num++] = blk;
        }
    }

    if (indirectBlk != -1) {
        fseek(fd, DATA_START + indirectBlk * BLOCK_SIZE, SEEK_SET);
        fwrite(indirectBlocks, sizeof(unsigned int), 256, fd);
    }

    // 写入新 inode
    time_t t = time(0);
    strftime(newInode.time, sizeof(newInode.time), "%Y/%m/%d %X %A %jday", localtime(&t));
    fseek(fd, INODE_START + newIno * INODE_SIZE, SEEK_SET);
    fwrite(&newInode, sizeof(newInode), 1, fd);

    // 更新目录
    strcpy(destDir.fileName[dir_entry_slot], destName.c_str());
    destDir.inodeID[dir_entry_slot] = newIno;

    inode parentInode;
    fseek(fd, INODE_START + destDirInodeID * INODE_SIZE, SEEK_SET);
    fread(&parentInode, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + parentInode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(&destDir, sizeof(directory), 1, fd);

    // 更新位图和超级块
    inode_bitmap[newIno] = 1;
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(inode_bitmap), 1, fd);

    superBlock.s_num_finode--;
    superBlock.s_num_fblock -= totalBlocks + (indirectBlk != -1 ? 0 : 1);
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(superBlock), 1, fd);

    inode temp;
    fseek(fd, INODE_START + currentDirectory.inodeID[0] * INODE_SIZE, SEEK_SET);
    fread(&temp, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + temp.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);

    printf("\033[32m[Info] Successfully copy file: %s -> %s\033[0m\n", srcName.c_str(), destName.c_str());

    return true;
}

//打开任意目录下的文件
inode* OpenFile(char* filepath)
{
    //文件路径检测
    if (filepath == NULL)
    {
        printf("\033[31m[Read Error] The file name or file path cannot be empty!\033[0m\n");
        return NULL;
    }

    //路径判断
    if (filepath[0] != '/' && filepath[0] != '.') {
        if (filepath == NULL || strlen(filepath) > FILE_NAME_LENGTH) {
            printf("\033[31m[Read Error] Illegal file name!\033[0m\n");
            return NULL;
        }

        //1. 查找是否存在该文件.
        int pos_in_directory = -1;
        inode* tmp_file_inode = new inode;
        do {
            pos_in_directory++;
            for (; pos_in_directory < DIRECTORY_NUM; pos_in_directory++)
            {
                if (strcmp(currentDirectory.fileName[pos_in_directory], filepath) == 0)
                {
                    break;
                }
            }
            if (pos_in_directory == DIRECTORY_NUM)
            {
                printf("\033[31m[Read Error] Could not find the file!\033[0m\n");
                return NULL;
            }
            
            // 2. 判断inode是否是目录
            int tmp_file_ino = currentDirectory.inodeID[pos_in_directory];
            fseek(fd, INODE_START + tmp_file_ino * INODE_SIZE, SEEK_SET);
            fread(tmp_file_inode, sizeof(inode), 1, fd);
        } while (tmp_file_inode->di_mode == 0);
        
        return tmp_file_inode;
    }
    else{
        inode* current_inode = new inode;
        inode* file_inode, *dir_inode;
        directory tmp_dir;
        
        // 获取开始目录
        char* token = strtok(filepath, "/");
        char* dir_name;
        dir_name = token;
        if (dir_name == NULL) {
            printf("\033[31m[Read Error] Illegal file path!\033[0m\n");
            return NULL;
        }

        if (filepath[0] == '/')
            fseek(fd, INODE_START, SEEK_SET);
        else if (strcmp(dir_name, ".") == 0)
            fseek(fd, INODE_START + currentDirectory.inodeID[0] * INODE_SIZE, SEEK_SET);
        else if (strcmp(dir_name, "..") == 0)
            fseek(fd, INODE_START + currentDirectory.inodeID[1] * INODE_SIZE, SEEK_SET);
        else {
            printf("\033[31m[Read Error] Illegal file path!\033[0m\n");
            return NULL;
        }
        fread(current_inode, sizeof(inode), 1, fd);

        // 遍历路径中的每个目录部分
        while (token != NULL)   {
            file_inode = dir_inode = NULL;
            dir_name = token;
            token = strtok(NULL, "/");

            fseek(fd, DATA_START + current_inode->di_addr[0] * BLOCK_SIZE, SEEK_SET);
            fread(&tmp_dir, sizeof(directory), 1, fd);
            for (int pos_in_directory = 0; pos_in_directory < DIRECTORY_NUM; pos_in_directory++) {
                if (strcmp(tmp_dir.fileName[pos_in_directory], dir_name) == 0) {
                        inode* tmp_inode = new inode;
                        int tmp_i = tmp_dir.inodeID[pos_in_directory];
                        fseek(fd, INODE_START + tmp_i * INODE_SIZE, SEEK_SET);
                        fread(tmp_inode, sizeof(inode), 1, fd);

                        //判断找到的是文件还是目录
                        if (tmp_inode->di_mode == 1)
                            file_inode = tmp_inode;
                        else
                            dir_inode = tmp_inode;
                    }
            }
            current_inode = dir_inode;

            if (!file_inode && !dir_inode) {
                printf("\033[31m[Read Error] Could not find the file!\033[0m\n");
                return NULL;
            }
        }

        if (!file_inode)
            printf("\033[31m[Read Error] Could not find the file!\033[0m\n");
        return file_inode;
    }
};

//输出文件信息
void PrintFile(inode& ifile)
{
    int block_num = ceil((float)ifile.di_size / BLOCK_SIZE);
    int print_line_num = 0;
    //从块中读取文件
    char stack[BLOCK_SIZE];
    if (block_num <= NADDR - 1)
    {
        for (int i = 0; i < block_num; i++)
        {
            fseek(fd, DATA_START + ifile.di_addr[i] * BLOCK_SIZE, SEEK_SET);
            fread(stack, sizeof(stack), 1, fd);
            for (int j = 0; j < BLOCK_SIZE; j++)
            {
                if (stack[j] == '\0')break;
                if (j % 128 == 0)
                {
                    printf("\n");
                    printf("%d\t", ++print_line_num);
                }
                printf("%c", stack[j]);
            }
        }
    }
    else if (block_num > NADDR - 1) {
        for (int i = 0; i < NADDR - 1; i++)
        {
            fseek(fd, DATA_START + ifile.di_addr[i] * BLOCK_SIZE, SEEK_SET);
            fread(stack, sizeof(stack), 1, fd);
            for (int j = 0; j < BLOCK_SIZE; j++)
            {
                if (stack[j] == '\0')break;
                if (j % 128 == 0)
                {
                    printf("\n");
                    printf("%d\t", ++print_line_num);
                }
                printf("%c", stack[j]);
            }
        }
        unsigned int f1[BLOCK_SIZE / sizeof(unsigned int)] = { 0 };
        fseek(fd, DATA_START + ifile.di_addr[NADDR - 1] * BLOCK_SIZE, SEEK_SET);
        fread(f1, sizeof(f1), 1, fd);
        for (int i = 0; i < block_num - (NADDR - 1); i++) {
            fseek(fd, DATA_START + f1[i] * BLOCK_SIZE, SEEK_SET);
            fread(stack, sizeof(stack), 1, fd);
            for (int j = 0; j < BLOCK_SIZE; j++)
            {
                if (stack[j] == '\0')break;
                if (j % 128 == 0)
                {
                    printf("\n");
                    printf("%d\t", ++print_line_num);
                }
                printf("%c", stack[j]);
            }
        }
    }
    printf("\n\n");
};

//打开目录
bool OpenDir_Specify(const char* dirname)
{
    //参数检测
    if (dirname == NULL || strlen(dirname) > FILE_NAME_LENGTH)
    {
        printf("\033[31m[Directory Name Error] Illegal directory name!\033[0m\n");
        return false;
    }

    //----------1.查找目录----------
    int pos_in_directory = 0;
    inode tmp_dir_inode;
    int tmp_dir_ino;
    do
    {
        for (; pos_in_directory < DIRECTORY_NUM; pos_in_directory++)
        {
            if (strcmp(currentDirectory.fileName[pos_in_directory], dirname) == 0)
                break;
        }
        if (pos_in_directory == DIRECTORY_NUM)
        {
            printf("\033[31m[Directory Open Error] Directory not found!\033[0m\n");
            return false;
        }
        
        //读取inode（跳过同名的普通文件）
        tmp_dir_ino = currentDirectory.inodeID[pos_in_directory];
        fseek(fd, INODE_START + tmp_dir_ino * INODE_SIZE, SEEK_SET);
        fread(&tmp_dir_inode, sizeof(inode), 1, fd);
    } while (tmp_dir_inode.di_mode == 1); //跳过同名的普通文件(模式=1)

    //----------2.更新当前目录----------
    directory new_current_dir;
    fseek(fd, DATA_START + tmp_dir_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fread(&new_current_dir, sizeof(directory), 1, fd);
    currentDirectory = new_current_dir;
    if (dirname[0] == '.' && dirname[1] == 0)
    {
        dir_pointer; //空操作 
    }
    else if (dirname[0] == '.' && dirname[1] == '.' && dirname[2] == 0)
    {
        if (dir_pointer != 0)
            dir_pointer--;
    }
    else
    {
        for (int i = 0; i < 14; i++)
            ab_dir[dir_pointer][i] = dirname[i];
        dir_pointer++;
    }
    return true;
};

//此函数执行成功是false，执行失败是true
bool OpenDir(const char* dirname)
{
    // 保存当前工作目录，用于后续恢复
    directory savedDir = currentDirectory;
    int savedPointer = dir_pointer;

    char dir_path[100][14] = {0}; //分割路径
    // 创建临时可修改的拷贝（因为strtok会修改原始字符串）
    char temp[100];
    strcpy(temp, dirname);

    int count = 0; // 用于计数存储的分段数量*
    //使用strtok进行分割
    char* token = strtok(temp, "/");
    while (token != nullptr && count < 100)
    {
        // 检查长度是否超过限制（包括结尾的null字符）
        if (strlen(token) < 13)
        {
            strcpy(dir_path[count], token);
        }
        else
        {
            // 如果超过长度，截断处理
            strncpy(dir_path[count], token, 13);
            dir_path[count][13] = '\0'; // 确保结束符
        }
        count++;
        token = strtok(nullptr, "/"); // 继续分割剩余部分
    }

    if(dirname[0] == '/') //绝对路径
    {
        // 切换到根目录
        fseek(fd, INODE_START, SEEK_SET);
        inode root_inode;
        fread(&root_inode, sizeof(inode), 1, fd);
        fseek(fd, DATA_START + root_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
        directory root_dir;
        fread(&root_dir, sizeof(directory), 1, fd);
        currentDirectory = root_dir;
        dir_pointer = 1; //?重置绝对路径指针
    }

    //开始循环进入各级目录
    for(int i=0; i<count; i++)
    {
        if(!OpenDir_Specify(dir_path[i]))
        {
            currentDirectory = savedDir;
            dir_pointer = savedPointer;
            fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
            fread(&currentDirectory, sizeof(directory), 1, fd);
            return true;
        }
    }
    fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);
    return false;
}

//创建新目录，返回值为布尔
bool MakeDir_Specify(const char* dirname)
{
    //参数检测（判断有没有非法目录名）
    if (dirname == NULL || strlen(dirname) > FILE_NAME_LENGTH)
    {
        printf("\033[31m[Directory Name Error] Illegal directory name!\033[0m\n");
        return false;
    }
    
    //----------1.查看剩余空间是否充足（即检查inode是否用光）----------
    //检查超级块中空闲块数和空闲inode数
    if (superBlock.s_num_fblock <= 0 || superBlock.s_num_finode <= 0)
    {
        printf("\033[31m[Space Error] Insufficient space to create directory!\033[0m\n");
        return false;
    }
    //​​资源分配​​：遍历inode位图查找空闲inode号（new_ino），调用find_free_block（这个函数在前面定义了）获取空闲块地址
    int new_ino = 0;
    unsigned int new_block_addr = 0; //重要*
    for (; new_ino < INODE_NUM; new_ino++)
        if (inode_bitmap[new_ino] == 0)
            break;
    find_free_block(new_block_addr);
    if (new_block_addr == -1)
        return false;
    //最后判断是否能够分配新资源，若无空闲inode号或者无空闲块，返回false
    if (new_ino == INODE_NUM || new_block_addr == BLOCK_NUM)
    {
        printf("\033[31m[Directory Creation Error] Lack of valid spaces!\033[0m\n");
        return false;
    }
    
    //----------2.检查目录名在当前目录是否有重名----------
    //遍历当前目录项查找同名项
    for (int i = 0; i < DIRECTORY_NUM; i++)
    {
        //特别处理：允许目录与文件同名
        if (strcmp(currentDirectory.fileName[i], dirname) == 0) //发现同名项时进入处理逻辑
        {
            inode* tmp_file_inode = new inode; //语法?
            int tmp_file_ino = currentDirectory.inodeID[i];
            fseek(fd, INODE_START + tmp_file_ino * INODE_SIZE, SEEK_SET); //定位磁盘上的inode位置（SEEK_SET：基准位置为文件开头）
            fread(tmp_file_inode, sizeof(inode), 1, fd); //从磁盘读取inode数据
            if (tmp_file_inode->di_mode == 1)
            {
                //同名的是文件：跳过
                continue;
            }
            else
            {
                //同名的是目录：报错返回
                printf("\033[31m[Directory Creation Error] Directory name '%s' has been used!\033[0m\n", currentDirectory.fileName[i]);
                return false;
            }
        }
    }
    
    //----------3.检查当前目录项是否超过指定数额（太多了）----------
    int itemCounter = 0;
    for (int i = 0; i < DIRECTORY_NUM; i++)
        if (strlen(currentDirectory.fileName[i]) > 0)
            itemCounter++;
    if (itemCounter >= DIRECTORY_NUM)
    {
        printf("\033[31m[Directory Creation Error]Too many files or directories in current path!\033[0m\n");
        return false;
    }
    
    //----------4.创建新inode----------
    inode idir_tmp;
    idir_tmp.i_ino = new_ino;
    idir_tmp.di_number = 1; //指向该目录的​​链接
    idir_tmp.di_mode = 0; //0 代表目录
    idir_tmp.di_size = sizeof(directory);
    memset(idir_tmp.di_addr, -1, sizeof(unsigned int) * NADDR); //初始化块指针数组
    idir_tmp.di_addr[0] = new_block_addr; //设置第一个数据块地址
    //修改时间信息
    time_t t = time(0);
    strftime(idir_tmp.time, sizeof(idir_tmp.time), "%Y/%m/%d %X %A %jday %z", localtime(&t));
    idir_tmp.time[64] = 0;
    idir_tmp.icount = 0; //初始化内存引用计数
    idir_tmp.permission = USER_PERMISSION; //设置最大权限位(?)
    fseek(fd, INODE_START + new_ino * INODE_SIZE, SEEK_SET); //定位磁盘写入位置
    fwrite(&idir_tmp, sizeof(inode), 1, fd); //写入 inode 数据到磁盘
    
    //----------5.创建新目录文件----------
    directory tmp_dir; //创建内存目录结构体
    memset(tmp_dir.fileName, 0, sizeof(char) * DIRECTORY_NUM * FILE_NAME_LENGTH); //初始化文件名数组（内存清零）
    memset(tmp_dir.inodeID, -1, sizeof(unsigned int) * DIRECTORY_NUM); //初始化inode数组
    strcpy(tmp_dir.fileName[0], "."); //添加当前目录项（指向自身的）
    tmp_dir.inodeID[0] = new_ino;
    strcpy(tmp_dir.fileName[1], ".."); //设置父目录 inode
    tmp_dir.inodeID[1] = currentDirectory.inodeID[0]; //inodeID[0]是父目录自身inode（对应父目录的"."项）
    fseek(fd, DATA_START + new_block_addr * BLOCK_SIZE, SEEK_SET);
    fwrite(&tmp_dir, sizeof(directory), 1, fd);
    
    //----------6.更新bitmap----------
    inode_bitmap[new_ino] = 1;
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(unsigned short) * INODE_NUM, 1, fd);
    
    //----------7.更新目录----------
    //首先定位父目录inode
    int pos_directory_inode = 0;
    pos_directory_inode = currentDirectory.inodeID[0]; //"."
    inode tmp_directory_inode;
    fseek(fd, INODE_START + pos_directory_inode * INODE_SIZE, SEEK_SET);
    fread(&tmp_directory_inode, sizeof(inode), 1, fd);
    //其次添加新目录项到父目录（也就是自身）
    for (int i = 2; i < DIRECTORY_NUM; i++)
    {
        if (strlen(currentDirectory.fileName[i]) == 0)
        {
            strcat(currentDirectory.fileName[i], dirname);
            currentDirectory.inodeID[i] = new_ino;
            break;
        }
    }
    fseek(fd, DATA_START + tmp_directory_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(&currentDirectory, sizeof(directory), 1, fd);
    directory tmp_directory = currentDirectory;
    int tmp_pos_directory_inode = pos_directory_inode;
    //开始循环，要更新当前目录的所有上级目录，直至根目录
    while (true)
    {
        tmp_directory_inode.di_number++; //硬链接+1
        //刷新包含新链接计数的inode元数据
        fseek(fd, INODE_START + tmp_pos_directory_inode * INODE_SIZE, SEEK_SET);
        fwrite(&tmp_directory_inode, sizeof(inode), 1, fd);
        //若为根目录，则break
        if (tmp_directory.inodeID[1] == tmp_directory.inodeID[0])
            break;
        //否则，继续向上
        tmp_pos_directory_inode = tmp_directory.inodeID[1]; //".."
        fseek(fd, INODE_START + tmp_pos_directory_inode * INODE_SIZE, SEEK_SET);
        fread(&tmp_directory_inode, sizeof(inode), 1, fd);
        fseek(fd, DATA_START + tmp_directory_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
        fread(&tmp_directory, sizeof(directory), 1, fd);
    }
    
    //----------8.更新超级块----------
    superBlock.s_num_finode--;
    fseek(fd, BLOCK_SIZE, SEEK_SET);
    fwrite(&superBlock, sizeof(filsys), 1, fd);

    // // 更新当前目录
    // fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
    // fread(&currentDirectory, sizeof(directory), 1, fd);
    
    return true;
};

bool MakeDir(const char* dirname)
{
    // 保存当前工作目录，用于后续恢复
    directory savedDir = currentDirectory;
    int savedPointer = dir_pointer;

    char dir_path[100][14] = {0}; //分割路径

    // 创建临时可修改的拷贝（因为strtok会修改原始字符串）
    char temp[100];
    strcpy(temp, dirname);

    int count = 0; // 用于计数存储的分段数量*
    
    // 使用strtok进行分割
    char* token = strtok(temp, "/");
    while (token != nullptr && count < 100) {
        // 检查长度是否超过限制（包括结尾的null字符）
        if (strlen(token) < 13) {
            strcpy(dir_path[count], token);
        } else {
            // 如果超过长度，截断处理
            strncpy(dir_path[count], token, 13);
            dir_path[count][13] = '\0'; //确保结束符
        }
        
        count++;
        token = strtok(nullptr, "/"); //继续分割剩余部分
    }

    if(dirname[0] == '/') //绝对路径
    {
        int start_cnt = 0;
        OpenDir("/");
        for(int c=0; c<count; c++)
        {
            bool found_same_name_dir = false;
            //遍历当前（×，实际上应该是“当前”）目录项查找同名项
            for (int i = 0; i < DIRECTORY_NUM; i++)
            {
                //特别处理：允许目录与文件同名
                if (strcmp(currentDirectory.fileName[i], dir_path[c]) == 0) //发现同名项时进入处理逻辑
                {
                    inode* tmp_file_inode = new inode; //语法?
                    int tmp_file_ino = currentDirectory.inodeID[i];
                    fseek(fd, INODE_START + tmp_file_ino * INODE_SIZE, SEEK_SET); //定位磁盘上的inode位置（SEEK_SET：基准位置为文件开头）
                    fread(tmp_file_inode, sizeof(inode), 1, fd); //从磁盘读取inode数据
                    if (tmp_file_inode->di_mode == 1)
                    {
                        //同名的是文件：跳过
                        continue;
                    }
                    else
                    {
                        if(c==(count-1))
                        {
                            //同名目录：报错返回
                            printf("\033[31m[Directory Creation Error] Directory name '%s' has been used!\033[0m\n", currentDirectory.fileName[i]);
                            currentDirectory = savedDir;
                            dir_pointer = savedPointer;
                            fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
                            fread(&currentDirectory, sizeof(directory), 1, fd);
                            return false;
                        }
                        else
                        {
                            //不是最后一级目录
                            found_same_name_dir = true;
                            continue;
                        }
                    }
                }
            }
            //没有同名项目录了，去创建新目录
            if(!found_same_name_dir)
            {
                for(int j=c; j<count; j++)
                {
                    if(!MakeDir_Specify(dir_path[j]))
                    {
                        currentDirectory = savedDir;
                        dir_pointer = savedPointer;
                        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
                        fread(&currentDirectory, sizeof(directory), 1, fd);
                        return false;
                    }
                    if(OpenDir(dir_path[j]))
                    {
                        currentDirectory = savedDir;
                        dir_pointer = savedPointer;
                        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
                        fread(&currentDirectory, sizeof(directory), 1, fd);
                        return false;
                    }
                }
                break;
            }
            else
            {
                OpenDir(dir_path[c]);
            }
        }
    }
    else
    {
        for(int c=0; c<count; c++)
        {
            if(!MakeDir_Specify(dir_path[c]))
            {
                currentDirectory = savedDir;
                dir_pointer = savedPointer;
                fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
                fread(&currentDirectory, sizeof(directory), 1, fd);
                return false;
            }
            if(OpenDir(dir_path[c]))
            {
                currentDirectory = savedDir;
                dir_pointer = savedPointer;
                fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
                fread(&currentDirectory, sizeof(directory), 1, fd);
                return false;
            }
        }
    }
    currentDirectory = savedDir; // 恢复原目录
    dir_pointer = savedPointer;
    fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);
    return true;
}

// 辅助函数：检查路径是否相等或是祖先
bool isAncestor(const char* ancestor, const char* descendant) {
    size_t ancestorLen = strlen(ancestor);
    if (strncmp(ancestor, descendant, ancestorLen) != 0) {
        return false;
    }

    // 检查是完全匹配还是路径分隔符
    return (descendant[ancestorLen] == '\0') ||
        (descendant[ancestorLen] == '/');
}

bool RemoveDir(const char* dirname_input) {
    // ==================== 保存当前目录状态 ====================
    int original_dir_pointer = dir_pointer;
    char original_ab_dir[DIRECTORY_NUM][FILE_NAME_LENGTH];
    for (int i = 0; i < DIRECTORY_NUM; i++) {
        strcpy(original_ab_dir[i], ab_dir[i]);
    }
    directory originalDirectory = currentDirectory;

    // ==================== 处理路径和检查 ====================
    char current_path[256] = "";
    if (dir_pointer == 0) {
        strcpy(current_path, "/");
    }
    else {
        for (int i = 0; i < dir_pointer; i++) {
            strcat(current_path, "/");
            strcat(current_path, original_ab_dir[i]);
        }
    }

    char target_abs_path[256] = "";
    if (dirname_input[0] == '/') {
        strcpy(target_abs_path, dirname_input);
    }
    else {
        strcpy(target_abs_path, current_path);
        if (strcmp(current_path, "/") != 0) {
            strcat(target_abs_path, "/");
        }
        strcat(target_abs_path, dirname_input);
    }

    // 检查目标路径是否是当前路径或其祖先
    if (isAncestor(target_abs_path, current_path)) {
        printf("\033[31m[Directory Delete Error] Cannot delete current directory or its ancestor!\033[0m\n");
        // 恢复状态后返回
        dir_pointer = original_dir_pointer;
        for (int i = 0; i < DIRECTORY_NUM; i++) {
            strcpy(ab_dir[i], original_ab_dir[i]);
        }
        currentDirectory = originalDirectory;
        // 更新当前目录（新修改）
        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
        fread(&currentDirectory, sizeof(directory), 1, fd);
        return false;
    }

    // ==================== 解析父目录和目录名 ====================
    char parent_dir[256] = { 0 };
    char dir_to_remove[FILE_NAME_LENGTH] = { 0 };

    // 找到最后一个'/'
    char* last_slash = strrchr(target_abs_path, '/');
    if (!last_slash)
    {
        // 没有斜杠，表示在根目录
        strcpy(parent_dir, "/");
        strncpy(dir_to_remove, target_abs_path, FILE_NAME_LENGTH - 1);
        dir_to_remove[FILE_NAME_LENGTH - 1] = '\0';
    }
    else if (last_slash == target_abs_path)
    {
        // 根目录的情况 (如 "/a")
        strcpy(parent_dir, "/");
        strncpy(dir_to_remove, last_slash + 1, FILE_NAME_LENGTH - 1);
        dir_to_remove[FILE_NAME_LENGTH - 1] = '\0';
    }
    else
    {
        // 截取父目录路径
        strncpy(parent_dir, target_abs_path, last_slash - target_abs_path);
        parent_dir[last_slash - target_abs_path] = '\0';
        // 获取要删除的目录名
        strncpy(dir_to_remove, last_slash + 1, FILE_NAME_LENGTH - 1);
        dir_to_remove[FILE_NAME_LENGTH - 1] = '\0';
    }

    // ==================== 切换到父目录 ====================
    if (OpenDir(parent_dir)) {
        printf("\033[31m[Open Parent Directory Error] Failed to open parent directory!\033[0m\n");
        dir_pointer = original_dir_pointer;
        for (int i = 0; i < DIRECTORY_NUM; i++) {
            strcpy(ab_dir[i], original_ab_dir[i]);
        }
        currentDirectory = originalDirectory;
        // 更新当前目录（新修改）
        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
        fread(&currentDirectory, sizeof(directory), 1, fd);
        return false;
    }

    // ==================== 执行删除操作 ====================
    // 查找目录项
    int pos_in_directory = -1;
    for (int i = 0; i < DIRECTORY_NUM; i++) {
        if (strcmp(currentDirectory.fileName[i], dir_to_remove) == 0) {
            pos_in_directory = i;
            break;
        }
    }
    if (pos_in_directory == -1) {
        printf("\033[31m[Directory Open Error] Directory '%s' not found in parent!\033[0m\n", dir_to_remove);
        dir_pointer = original_dir_pointer;
        for (int i = 0; i < DIRECTORY_NUM; i++) {
            strcpy(ab_dir[i], original_ab_dir[i]);
        }
        currentDirectory = originalDirectory;
        // 更新当前目录（新修改）
        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
        fread(&currentDirectory, sizeof(directory), 1, fd);
        return false;
    }

    // 获取目录的inode
    int dir_ino = currentDirectory.inodeID[pos_in_directory];
    inode dir_inode;
    fseek(fd, INODE_START + dir_ino * INODE_SIZE, SEEK_SET);
    fread(&dir_inode, sizeof(inode), 1, fd);

    // 检查是否为目录
    if (dir_inode.di_mode == 1) {
        printf("\033[31m[Format Error] Target '%s' is not a directory!\033[0m\n", dir_to_remove);
        dir_pointer = original_dir_pointer;
        for (int i = 0; i < DIRECTORY_NUM; i++) {
            strcpy(ab_dir[i], original_ab_dir[i]);
        }
        currentDirectory = originalDirectory;
        // 更新当前目录（新修改）
        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
        fread(&currentDirectory, sizeof(directory), 1, fd);
        return false;
    }

    // 减少硬链接计数
    dir_inode.icount--;
    fseek(fd, INODE_START + dir_ino * INODE_SIZE, SEEK_SET);
    fwrite(&dir_inode, sizeof(inode), 1, fd);

    // 删除父目录中的目录项
    memset(currentDirectory.fileName[pos_in_directory], 0, FILE_NAME_LENGTH);
    currentDirectory.inodeID[pos_in_directory] = -1;

    // 更新父目录inode
    int parent_ino = currentDirectory.inodeID[0];
    inode parent_inode;
    fseek(fd, INODE_START + parent_ino * INODE_SIZE, SEEK_SET);
    fread(&parent_inode, sizeof(inode), 1, fd);
    fseek(fd, DATA_START + parent_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(&currentDirectory, sizeof(directory), 1, fd);

    // 若存在硬链接则直接返回
    if (dir_inode.icount > 0) {
        // 恢复原始目录状态
        dir_pointer = original_dir_pointer;
        for (int i = 0; i < DIRECTORY_NUM; i++) {
            strcpy(ab_dir[i], original_ab_dir[i]);
        }
        currentDirectory = originalDirectory;
        // 更新当前目录（新修改）
        fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
        fread(&currentDirectory, sizeof(directory), 1, fd);
        return true;
    }

    // 递归删除子项
    directory child_dir;
    fseek(fd, DATA_START + dir_inode.di_addr[0] * BLOCK_SIZE, SEEK_SET);
    fread(&child_dir, sizeof(directory), 1, fd);
    for (int i = 2; i < DIRECTORY_NUM; i++) {
        if (strlen(child_dir.fileName[i]) > 0) {
            char* child_name = child_dir.fileName[i];
            int child_ino = child_dir.inodeID[i];
            inode child_inode;
            fseek(fd, INODE_START + child_ino * INODE_SIZE, SEEK_SET);
            fread(&child_inode, sizeof(inode), 1, fd);

            // 保存当前目录上下文
            directory oldDir = currentDirectory;
            currentDirectory = child_dir;

            if (child_inode.di_mode == 1)
                DeleteFile(child_name);
            else
                RemoveDir(child_name);

            // 恢复上下文
            currentDirectory = oldDir;
        }
    }

    // 释放资源
    for (int i = 0; i < NADDR; i++) {
        if (dir_inode.di_addr[i] != 0)
            recycle_block(dir_inode.di_addr[i]);
    }

    // 清空inode
    char zero_inode[INODE_SIZE] = { 0 };
    fseek(fd, INODE_START + dir_ino * INODE_SIZE, SEEK_SET);
    fwrite(zero_inode, INODE_SIZE, 1, fd);

    // 更新inode bitmap
    inode_bitmap[dir_ino] = 0;
    fseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, sizeof(inode_bitmap), 1, fd);

    // 更新超级块
    superBlock.s_num_finode++;
    fseek(fd, 0, SEEK_SET);
    fwrite(&superBlock, sizeof(filsys), 1, fd);

    // ==================== 恢复原始目录状态 ====================
    dir_pointer = original_dir_pointer;
    for (int i = 0; i < DIRECTORY_NUM; i++) {
        strcpy(ab_dir[i], original_ab_dir[i]);
    }
    currentDirectory = originalDirectory;

    // 更新当前目录（新修改）
    fseek(fd, DATA_START + currentDirectory.inodeID[0] * BLOCK_SIZE, SEEK_SET);
    fread(&currentDirectory, sizeof(directory), 1, fd);

    return true;
}

// 展示当前路径下的子路径和文件信息
void List(){
    printf("     name\t\tinodeID\t\tsize\t\tpermission\t\ttime\n");
    for (int i = 0; i < DIRECTORY_NUM; i++)
    {
        if (strlen(currentDirectory.fileName[i]) > 0)
        {
            inode tmp_inode;
            fseek(fd, INODE_START + currentDirectory.inodeID[i] * INODE_SIZE, SEEK_SET);
            fread(&tmp_inode, sizeof(inode), 1, fd);
            const char* tmp_type = tmp_inode.di_mode == 0 ? "DIRECTORY-" : "FILE-";
            printf("%10s\t\t%d\t\t%u\t\t%s", currentDirectory.fileName[i], tmp_inode.i_ino, tmp_inode.di_size, tmp_type);
            if (tmp_inode.permission & 1) printf("USER\t\t");
            else printf("SYSTEM\t\t");
            printf("%s\n", tmp_inode.time);
        }
    }
    printf("\n");
}

//读取文件权限
bool filePermission(const char* filepath) {
    inode* target_inode = new inode;
    //路径判断
    if (filepath[0] != '/' && filepath[0] != '.') {

        if (filepath == NULL || strlen(filepath) > FILE_NAME_LENGTH) {
            return false;
        }
        //1. 查找是否存在该文件.
        int pos_in_directory = 2;
        for (; pos_in_directory < DIRECTORY_NUM; pos_in_directory++) {
            if (strcmp(currentDirectory.fileName[pos_in_directory], filepath) == 0) {
                    break;
                }
            }
        if (pos_in_directory == DIRECTORY_NUM) {
                printf("\033[31m[System Error] Could not find the file!\033[0m\n");
                return false;
        }
            
        int tmp_file_ino = currentDirectory.inodeID[pos_in_directory];
        fseek(fd, INODE_START + tmp_file_ino * INODE_SIZE, SEEK_SET);
        fread(target_inode, sizeof(inode), 1, fd);
    }
    else{

        inode* current_inode = new inode;
        inode* file_inode, *dir_inode;
        directory tmp_dir;

        // 获取开始目录
        char tmp[1024];
        memset(tmp, 0, sizeof tmp);
        strcpy(tmp, filepath);
        char* token = strtok(tmp, "/");
        char* dir_name;
        dir_name = token;
        if (dir_name == NULL) {
            printf("\033[31m[System Error] Illegal File path!\033[0m\n");
            return false;
        }

        if (filepath[0] == '/')
            fseek(fd, INODE_START, SEEK_SET);
        else if (strcmp(dir_name, ".") == 0)
            fseek(fd, INODE_START + currentDirectory.inodeID[0] * INODE_SIZE, SEEK_SET);
        else if (strcmp(dir_name, "..") == 0)
            fseek(fd, INODE_START + currentDirectory.inodeID[1] * INODE_SIZE, SEEK_SET);
        else {
            printf("\033[31m[System Error] Illegal File path!\033[0m\n");
            return false;
        }
        fread(current_inode, sizeof(inode), 1, fd);
         

        // 遍历路径中的每个目录部分
        while (token != NULL)   {
            file_inode = dir_inode = NULL;
            dir_name = token;
            token = strtok(NULL, "/");

            fseek(fd, DATA_START + current_inode->di_addr[0] * BLOCK_SIZE, SEEK_SET);
            fread(&tmp_dir, sizeof(directory), 1, fd);
            for (int pos_in_directory = 0; pos_in_directory < DIRECTORY_NUM; pos_in_directory++) {
                if (strcmp(tmp_dir.fileName[pos_in_directory], dir_name) == 0) {
                        inode* tmp_inode = new inode;
                        int tmp_i = tmp_dir.inodeID[pos_in_directory];
                        fseek(fd, INODE_START + tmp_i * INODE_SIZE, SEEK_SET);
                        fread(tmp_inode, sizeof(inode), 1, fd);

                        //判断找到的是文件还是目录
                        if (tmp_inode->di_mode == 1)
                            file_inode = tmp_inode;
                        else
                            dir_inode = tmp_inode;
                    }
            }
            current_inode = dir_inode;

            if (!file_inode && !dir_inode) {
                printf("\033[31m[System Error] Could not find the file!\033[0m\n");
                return false;
            }
        }

        if (!file_inode) {
            printf("\033[31m[System Error] Could not find the file!\033[0m\n");
            return false;
        }
        target_inode = file_inode;
    }
    if (target_inode->permission & 1) 
        return true;
    else {
        printf("\033[31m[System Error] The system file could not be modified!\033[0m\n");
        return false;
    }
}

int main(){
    memset(ab_dir, 0, sizeof(ab_dir));
    dir_pointer = 0;
    FILE* fs_test = fopen(filesystem, "r");
    if (fs_test == NULL)
    {
        printf("The file volume was not found Please wait, creating a new file volume...\n\n");
        Format();
    }
    
    Mount();
    printf("**************************************************************\n");
    printf("*                                                            *\n");
    printf("*           Welcome to the File Management System            *\n");
    printf("*           Tohsaka-Sakura               不会起名            *\n");
    printf("*           ShockWithAwe                SwordRain            *\n");
    printf("*                                                            *\n");
    printf("**************************************************************\n");
    ab_dir[dir_pointer][0] = '/';ab_dir[dir_pointer][1] = '\0';
    dir_pointer++;
    inode* currentInode = new inode;
    CommParser(currentInode);
    return 0;
}

void CommParser(inode*& currentInode){
    char para1[11];
    char para2[1024];
    char para3[1024];
    int block_num;
    while (true)
    {
        unsigned int f1[BLOCK_SIZE / sizeof(unsigned int)] = { 0 };
        fseek(fd, DATA_START + 8 * BLOCK_SIZE, SEEK_SET);
        fread(f1, sizeof(f1), 1, fd);
        memset(para1, 0, 11);
        memset(para2, 0, 1024);
        memset(para3, 0, 1024);
        printf("\033[36m/\033[0m");
        for (int i = 1; i < dir_pointer; i++)
            printf("\033[36m%s/\033[0m", ab_dir[i]);
        printf("\033[36m>\033[0m");
        scanf("%s", para1);
        para1[10] = 0;
        if (strcmp("ls", para1) == 0)
        {
            List();
        }
        else if (strcmp("info", para1) == 0) {
            for (int i = 0; i < 300; i++)
            {
                if (i > superBlock.special_free)printf("-1\t");
                else printf("%d\t", superBlock.special_stack[i]);
                if (i % 10 == 9)printf("\n");
            }
            printf("System Info:\nTotal Blocks:%d\nFree BlockS:%d\nTotal Inode:%d\nFree Inode:%d\n\n", superBlock.s_num_block, superBlock.s_num_fblock, superBlock.s_num_inode, superBlock.s_num_finode);
            printf("\n\n");
        }
        else if (strcmp("create", para1) == 0)
        {
            scanf("%s", para2);
            scanf("%d", &block_num);
            para2[1023] = 0;
            CreateFile(para2, block_num);
        }
        else if (strcmp("rm", para1) == 0)
        {
            scanf("%s", para2);
            para2[1023] = 0;
            if (filePermission(para2))
                DeleteFile(para2);
        }
        else if (strcmp("cp", para1) == 0) {
            scanf("%s", para2);
            scanf("%s", para3);
            para2[1023] = 0;
            para3[1023] = 0;
            if (filePermission(para2))
                Copy(para2, para3);
        }
        else if (strcmp("cat", para1) == 0) {
            scanf("%s", para2);
            para2[1023] = 0;
            currentInode = OpenFile(para2);
            if (currentInode)
                PrintFile(*currentInode);
        }
        else if (strcmp("cd", para1) == 0) {
            scanf("%s", para2);
            para2[1023] = 0;
            OpenDir(para2);
        }
        else if (strcmp("mkdir", para1) == 0) {
            scanf("%s", para2);
            para2[1023] = 0;

            MakeDir(para2);
        }
        else if (strcmp("rmdir", para1) == 0) {
            scanf("%s", para2);
            para2[1023] = 0;

            RemoveDir(para2);
        }
        else if (strcmp("exit", para1) == 0) {
            break;
        }
        else {
            printf("The system currently supports instructions: \n");
            printf("\t01.Exit System........................................................(exit)\n");
            printf("\t02.Show Help Info.....................................................(help)\n");
            printf("\t03.List Files and Directories...........................................(ls)\n");
            printf("\t04.Enter Other Directory......................................(cd + dirname)\n");
            printf("\t05.Create New Directory....................................(mkdir + dirpath)\n");
            printf("\t06.Delete Directory........................................(rmdir + dirpath)\n");
            printf("\t07.Create New File....................(create + filename + file size(block))\n");
            printf("\t08.Read File................................................(cat + filepath)\n");
            printf("\t09.Copy File..........................(cp + sourcefilepath + targetfilepath)\n");
            printf("\t10.Delete File...............................................(rm + filename)\n");
            printf("\t11.Show System Info...................................................(info)\n");
        }
    }
};
