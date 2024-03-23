#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<iostream>
#include<sys/shm.h>

using namespace std;

const int KB=1024;
const int MB=1024*KB;
const int DISK_SIZE=100*MB;

//���裺
//1.һ���ļ�ֻ�ܷ���һ��������
//2.һ����1KB
//3.bitmap inode diritem��С���Լ���� 

//�ѵ㣺
//1.���������ڴ棬����ƫ�������м���;
//2.������ɾ���ļ�����Ҫͬʱ�޸ĵĶ����ܶ� 

//���ܣ�
//1.ʵ�ֵ�ǰĿ¼�µĻ����ļ�����:pwd cd ls mkdir rmdir mv touch rmdir open read write �� 

//����
//1.�ļ���С�̶� 
//2.û��ʵ�ֿ���Ͷ༶����
//3. ��֧�ֵ���Ŀ¼


const int PER_DIR_ITEM=32;
const int MAX_LEN=1024/PER_DIR_ITEM;

const int PER_INODE=64;
const int PER_BLOCK=1*KB;
const int PER_GROUP=1*MB;	//�������char����bit��Ϊλͼ�Ļ���һ����װ��1024B==>1024�����Ϣ==��1MB����Ϣ

const int GROUP_NUM=DISK_SIZE/PER_GROUP;  //100*1024*1024   /  1024*1024=100
const int GROUP_BLOCK_NUM=1024;
const int GROUP_INODE_NUM=16;

const int ROOT_BLOCK=5;
const int KEY=0615;

//1024-1-100-1-1-1:1�������飬100��group�飬1��blockmap��1��inodemap��1��inodetable������Ϊ���ݿ�


//������洢ȫ����Ϣ��ÿ��������Ҫ���Լ��Ŀ��������飬��Ҫ�ڸ���������б���
//ռ��1��   
struct SuperBlock{
	int inodesCount;    //inode����
	int blocksCount;	//block����
	int rblocksCount;   //�����block����
	int freeBlockCount; //���е�block����
	int freeInodeCount; //���е�inode����
	int blocksPerGroup; //ÿ�������block����
};

//ÿ������ռ��1��
struct GroupBlock{
	int blockMapNum;       //blockmap�������̿�
	int inodeMapNum;  	   //inodeMap�������̿�
	int inodeTableNum;     //����������̿� 
	
	int freeBlockCount;	   //�ÿ����п���block������
	int freeInodeCount;    //�ÿ����п��е�inode������
	int dirNodeCount;	   //�ÿ�����Ŀ¼������
	//int pad;
};



struct Inode{
	int len;	    //�ļ�ʵ������ĩβ(�鳤) ���� Ŀ¼��ĳ���
	int linkCount;  //���ڴ���Ӳ����,��ϵͳ�ݲ�֧�ִ˹���
	int size;     //�ļ��Ĵ�С�����̿�Ϊ��λ,Ĭ��Ϊ1
	int block[13];  //�洢���ݣ����Բ��ö༶��������ϵͳ�ݲ�֧�ִ˹���
};


struct DirItem{
	int inodeNum;	//���ļ���inode���
	int type;       //��һЩ�ļ����Ͳ���Ҫ�����ļ���ֻ��ҪĿ¼���iNode  0�����ļ���1�����ļ���
	char name[24];  //������24���ַ�
};


//��֧�������洢������ɾ��
//�����ļ���Ҫ�����С����һ���������ɸ���
class Disk{
private:
	int size;
	int numOfGroup;
	SuperBlock *sp;   //100��������      ֻ����Ϊһ�ַ�����ʽ����Ϊû�������ԡ�
	GroupBlock *gb;   //100*100��group��
	int shm_id;
	void *shm_buf=(void*)0;
	int curDirBlock;  //��ǰĿ¼�����̿�
    int curDirInode;  //��ǰĿ¼����Inode
	char curName[24]; //��ǰĿ¼����

public:
	Disk(){
		//Ĭ�ϴ���100MB
		this->size=DISK_SIZE;
		shm_id=shmget((key_t)KEY,DISK_SIZE,0666|IPC_CREAT);
		//�������ڴ�ӳ�䵽���̿ռ�
		shm_buf = shmat(shm_id, 0, 0);
		numOfGroup=GROUP_NUM;
		init();
		cout<<"Make a 100MB disk successfully!\n";
	}
	Disk(int size){
		this->size=size;
		//���������ڴ�
		shm_id=shmget((key_t)KEY,size,0666|IPC_CREAT);
		//�������ڴ�ӳ�䵽���̿ռ�
		numOfGroup=size/PER_GROUP;
		shm_buf = shmat(shm_id, 0, 0);
		init();
		cout<<"Make a "<<size<<"MB disk successfully!\n";
		
	};
	~Disk(){
		//���ӳ��
		shmdt(shm_buf);
		//ɾ�������ڴ�
		//shmctl(shmid,IPC_RMID,0);
		if(shmctl(shm_id,IPC_RMID,0)==-1)
			printf("Delete error.\n");
		cout<<"Delete a disk successfully!\n";
	};
	
	int getBlockNum(void *addr){return ((char*)addr-(char *)shm_buf)/PER_BLOCK;};
	void* getBlockAddr(int num){return shm_buf+num*PER_BLOCK;};
	Inode* getIndoeAddr(int num){
		//GROUP_INODE_NUM
		int group=num/GROUP_INODE_NUM;
		int offset=num%GROUP_INODE_NUM;
		return (Inode*)(getInodeTableAddr(group)+offset*sizeof(Inode));
	}
	int getInodeNum(Inode* addr){
		int group=getGroup(addr);
		int offset=(int)((char*)addr-(char*)shm_buf)%PER_GROUP;
		return group*GROUP_INODE_NUM+(offset-PER_BLOCK*(1+numOfGroup+1+1))/sizeof(Inode);
	}
	void *getBlockMapAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup)*PER_BLOCK;
	}
	void *getInodeMapAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup+1)*PER_BLOCK;
	}
	void *getInodeTableAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup+1+1)*PER_BLOCK;
	}
	int getGroup(void *addr){
		return (int)((char*)addr-(char*)shm_buf)/PER_GROUP;
	}
	void *getGroupBlockAddr(int group){
		return (char*)shm_buf+sizeof(SuperBlock)+group*sizeof(GroupBlock);
	}
	
	//�жϴ����Ƿ����㹻�Ŀռ�
	bool isDiskSpaceEnough(int size){
		SuperBlock* sp=(SuperBlock*)shm_buf;
		return sp->freeBlockCount>size&&sp->freeInodeCount>0;
	}
	
	//Ŀ¼Ĭ�����㹻��
	bool isDirSpaceEnough(){
		//pass
		return true;
	}
	
	//��ʵ�ֵ�����ȡblock��ͬʱ���и��¡�
	int getFreeBlock(){
		for(int i=0;i<numOfGroup;++i){
			GroupBlock* gb=(GroupBlock*)getGroupBlockAddr(i);
			//if(gb->freeBlockCount){ //ƾ������жϻ����ӳ�
				char* blockMap=(char*)getBlockMapAddr(i);
				for(int j=0;j<GROUP_BLOCK_NUM;++j){
					if(blockMap[j]=='0'){
						blockMap[j]='1';
						return i*GROUP_BLOCK_NUM+j;
					}
				}
			//}
		}
		return -1; 
	}; 
	
	//��ȡinode��ͬʱ���и��¡�
	int getFreeInode(){
		
		for(int i=0;i<numOfGroup;++i){
			GroupBlock* gb=(GroupBlock*)getGroupBlockAddr(i);
			if(gb->freeInodeCount){
				char* inodeMap=(char*)getInodeMapAddr(i);
				for(int j=0;j<GROUP_INODE_NUM;++j){
					if(inodeMap[j]=='0'){
						inodeMap[j]='1';
						return i*GROUP_INODE_NUM+j;
					}
				}
			}
		}
	};
	
	
	//���inodeMap
	void releaseInodeMap(int inodeNum){
		int group=inodeNum/GROUP_INODE_NUM;
		int offset=inodeNum%GROUP_INODE_NUM;
		char* map=(char*) getInodeMapAddr(group);
		map[offset]='0';
		
	};
		
	//���blockMap
	void releaseBlockMap(int inodeNum){
		Inode* inode=(Inode*) getIndoeAddr(inodeNum);
		
		for(int i=0;i<inode->len;++i){
			int group=inode->block[i]/GROUP_BLOCK_NUM;
			int offset=inode->block[i]/GROUP_BLOCK_NUM;
			char* map=(char*) getBlockMapAddr(group);
			map[offset]='0';	
		}
	};
	
	//���³���������              type:1 ���� -1ɾ��
	void updateSpAndBg(int size,int group,int type=1){
		for(int i=0;i<numOfGroup;++i){
			
			//���³�����
			SuperBlock* sp=(SuperBlock*)(shm_buf+i*PER_GROUP);
			sp->freeBlockCount-=size*type;		//todo:���ܻ᲻ͬ�飬������Ҫ�ж� 
			sp->freeInodeCount-=1*type;
			
			//�������
			GroupBlock* gb=(GroupBlock*)(sp+group*sizeof(GroupBlock));
			gb->freeBlockCount-=size*type;
		    gb->freeInodeCount-=1*type;	
		}
	}
	
	
	//��֧�ֵ�ǰĿ¼,���費��Խ��
	void *getFreeDir(){
		//���ݳ��ȣ����ص�ַ
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		//˳����¸���Ŀ¼����
		return getBlockAddr(curDirBlock)+cur->len*sizeof(DirItem);//ʵ������Ҫ����Խ�紦��
	}
	

	
	void init(){
	
		//�������߱��ݳ�����
		for(int i=0;i<numOfGroup;i++){
			
			sp=(SuperBlock *)(shm_buf+i*PER_GROUP);
			sp->inodesCount=PER_BLOCK/PER_INODE*numOfGroup;
			sp->blocksCount=size/PER_BLOCK;
			 //��ȥ�����顢��顢����bitmap��һ��inodeTable����ȥ��Ŀ¼
			sp->freeBlockCount=sp->blocksCount-(1+numOfGroup+3)*numOfGroup-1; 
			sp->freeInodeCount=sp->inodesCount-1; //��ȥ��Ŀ¼
			sp->blocksPerGroup=PER_GROUP/PER_BLOCK;
			
		}
		
		//�������߱������
		for(int i=0;i<numOfGroup;i++){
			for(int j=0;j<numOfGroup;++j){
		
				gb=(GroupBlock *)(shm_buf+i*PER_GROUP+
							sizeof(SuperBlock)+j*sizeof(GroupBlock));					
				gb->blockMapNum=getBlockNum((shm_buf+i*
							PER_GROUP+sizeof(SuperBlock)+numOfGroup*sizeof(GroupBlock)));
				//cout<<gb->blockMapNum<<endl;
				gb->inodeMapNum=gb->blockMapNum+1;
				gb->inodeTableNum=gb->inodeMapNum+1;
				gb->freeBlockCount=1024-1-100-1-1-1;
				gb->freeInodeCount=PER_BLOCK/PER_INODE;    
			}
		}

		//��ʼ��������map
		for(int i=0;i<numOfGroup;++i){
			//inodeMapȫΪ0
			char* inodeMap=(char*)getInodeMapAddr(i);
			for(int j=0;j<GROUP_INODE_NUM;++j){
				inodeMap[j]='0';
			}
		
			//blockMapռ����1+numOfGroup+1+1+1
			char *blockMap=(char*)getBlockMapAddr(i);
			
			for(int j=0;j<GROUP_BLOCK_NUM;++j){
				if(j<1+numOfGroup+1+1+1)
					blockMap[j]='1';
				else
					blockMap[j]='0';
			}		
		}
		
		//������Ŀ¼��inode��ź�block��Ų������¿�ʼ���㡿

		int blockNum=-1;
		int inodeNum=-1;
		
		if(isDiskSpaceEnough(1)){
			//1.ˢ�µ�0������Map
			//ˢ��inodeMap

			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);//3.����inodeTable
			
			//ˢ��blockmap
			inode->block[0]=ROOT_BLOCK;
			
			//2.ˢ�µ�0������free inode��block���Լ��������
			updateSpAndBg(1,0);
			
			//4.����Ŀ¼��
	
			DirItem* item=(DirItem *)getBlockAddr(ROOT_BLOCK);//1024+1024+1024...
			item->inodeNum=0;//16+16+16+16....
			item->type=1;
			strcpy(item->name,"."); //û��.. ˵�����Ǹ�Ŀ¼
			
			inode->len=1;
			inode->size=1;

		
		}else{
			cout<<"Disk space is not enough."<<endl;
		}
		
		curDirBlock=ROOT_BLOCK;
		curDirInode=0;
		strcpy(curName,"/"); //Ҫ��֤curname�пռ� 
	}
	
	
	//����·����ѯblock���������ַ
	int path2block(char* path){
		//pass
	}
	
	void pwd(){
		cout<<"The cur dir is :"<<curName<<endl;
	}
	
	//��֧�ֵ�ǰĿ¼��
	void cd(char *name){
		
		//����block�����һ��,����ҵ�:�滻
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			cout<<name<<endl;
			cout<<tmp->name<<endl;
			if(strcmp(name,tmp->name)==0&&tmp->type==1){
				//����curDirInode��curDirBlock
				curDirInode=tmp->inodeNum;
				cur=getIndoeAddr(curDirInode);
				curDirBlock=cur->block[0];
				strcpy(curName,name);
				cout<<"cd successfully:"<<name<<endl;
				return ;
			}
		}
		
		//����Ҳ���:���������Ϣ
		cout<<"Error: not found\n"<<endl;
		
	}
	
	//�����ļ���Ĭ�ϣ�����Ŀ¼�Ƿ����
	void* isExist(char* name,int type,int inode){
		//�ҵ�dirItem��Ӧ��λ��
		Inode* cur=getIndoeAddr(inode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==type){	
				return tmp;
			}
		}
		return NULL;	
	}
	
	void* isExist(char* name){
		//�ҵ�dirItem��Ӧ��λ��
		Inode* cur=getIndoeAddr(curDirInode);//�Ƿ�ֱ�Ӽ�¼��ַЧ�ʸ��ߣ�
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			//cout<<":"<<name<<endl;
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			//cout<<":"<<tmp->name<<endl;
			if(strcmp(name,tmp->name)==0&&tmp->type==0){	
				return tmp;
			}
		}
		return NULL;
	}
	
	void* isExist(char* name,int type){
		//�ҵ�dirItem��Ӧ��λ��
		Inode* cur=getIndoeAddr(curDirInode);//�Ƿ�ֱ�Ӽ�¼��ַЧ�ʸ��ߣ�
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==type){	
				return tmp;
			}
		}
		return NULL;
		
	}
	
void mkdir(char *name){
		
		if(isExist(name,1)){
			cout<<"The file has existed"<<endl;
		}
		//����·����Ѱ��
		//pass
		
		
		//��ǰ·���²���
		
		//����һ��Ŀ¼�� ����ΪĿ¼���ļ�����Inode�����Կ��Դ�źܶ࣬��Ĭ��ֻռһ��block��
		//����һ��inode ����inodemap  �������inodecount ���³������inodecount
		//����һ��block 
		//��block������: .. ���׵�inode type �ļ�
		int blockNum=-1;
		int inodeNum=-1;
		
		if(isDiskSpaceEnough(1)){
			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);
			inode->size=1;
			blockNum=getFreeBlock();
			inode->block[0]=blockNum;
			
			int group=getGroup(inode);
			updateSpAndBg(1,group);
			
			DirItem* item=(DirItem*)getFreeDir();
			Inode* cur=(Inode*)getIndoeAddr(curDirInode);
			cur->len++;
			
			item->inodeNum=inodeNum;
			item->type=1;
			strcpy(item->name,name);
			
			inode->len=2;
			item=(DirItem*)getBlockAddr(blockNum);
			strcpy(item->name,".");
			item->inodeNum=inodeNum;
			item->type=1;
			
			item=(DirItem*)(getBlockAddr(blockNum)+sizeof(DirItem));
			strcpy(item->name,"..");
			item->inodeNum=curDirInode;
			item->type=1;
			
			//cout<<getBlockAddr(blockNum)<<" "<<sizeof(DirItem)<<(void* )item<<endl;
			
		}else{
			cout<<"Disk space is not enough."<<endl;
		}
		
		
		
	}
	
	void rmdir(char* name){
		rmdir(name,curDirInode);
	}
	
	
	
	void rmdir(char *name,int fatherNode){
		//����·����Ѱ��
		//pass
		
		
		//�ڵ�ǰ·���²���
		DirItem* dir=(DirItem*)isExist(name,1,fatherNode);
		if(dir==NULL){
			cout<<"not found"<<endl;
			return;
		}
		
		//��Ҫ�ȵݹ�ɾ��Ŀ¼�����еĶ���
		//����һ��inode  �����
		//����һ��block  �����
		//����һ��Ŀ¼�� ɾ��Ŀ¼��
		//��block������: .. ���׵�inode type �ļ�	//����Ŀ��Բ��ù� ��Ҳ����Ϊʲô�����û�и����ܹ���ԭ��ԭ��
		
		
		//�ļ��������������Ҫ�ݹ�ɾ��
		Inode* inode=(Inode*)getIndoeAddr(dir->inodeNum);
		if(inode->len>2){
			//����. ..����Ҫɾ��		
			
			for(int i=2;i<inode->len;++i){
				DirItem* item=(DirItem*)(getBlockAddr(inode->block[0])+i*sizeof(DirItem));
				//�����ļ��еݹ�ɾ��
				if(item->type==1){
					rmdir(item->name,dir->inodeNum);
				}
				//�����ļ���ֱ��ɾ��
				else{
					rmfile(item->name,dir->inodeNum);
				}
			}		
		}
			
		//�ļ��б����������ļ�һ��ɾ��
		dir->type=0;
		rmfile(dir->name);
		
	}
	
    //������
	void mv(char *old_name,char *new_name){
		
		//����block�����һ��,����ҵ�:�滻
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			
			if(strcmp(old_name,tmp->name)==0){
				strcpy(tmp->name,new_name);
				cout<<"Rename successfully!"<<endl;
				return ;
			}
		}
		
		//����Ҳ���:���������Ϣ
		cout<<"Error: not found\n"<<endl;
			
	}

	
	void touch(char *name){
		int sizeOfFile=5;
		//����Ǿ���·��
		//pass
		
		//���ֻ�ǵ�ǰĿ¼
		//�鿴�Ƿ��Ѿ�����
		if(isExist(name)){
			cout<<"File has Existed"<<endl;
			return;
		}
		

		int blockNum=-1;
		int inodeNum=-1;
		
		//cout<<"û��ʵ�ֶ༶������size<10"<<endl;
		if(isDiskSpaceEnough(sizeOfFile)){
			//1.ˢ�µ�0������Map
			//ˢ��inodeMap
			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);//3.����inodeTable
			//ˢ��blockmap
			for(int i=0;i<sizeOfFile;++i){
				blockNum=getFreeBlock();
				inode->block[i]=blockNum;
			}
			
			//2.ˢ�µ�0������free inode��block���Լ��������
			//updateSpAndBg(size,group)
			int group=getGroup(inode);
			updateSpAndBg(sizeOfFile,group);
			
			//4.����Ŀ¼��
			DirItem* item=(DirItem*)getFreeDir();
			Inode* cur=(Inode*)getIndoeAddr(curDirInode);
			cur->len++;
			item->inodeNum=inodeNum;//16+16+16+16....
			item->type=0;
			strcpy(item->name,name);
			
			
			inode->len=0;
			inode->size=sizeOfFile;
		
			cout<<"Make successfully:"<<name<<endl;
		
		}else{
			cout<<"Disk space is not enough"<<endl;
		}	
		return ;

	}
	
	void rmfile(char *name,int fatherNode){
		
		DirItem* fileItem=(DirItem*)isExist(name,0,fatherNode);
		if(fileItem==NULL){
			cout<<"Error: not found "<<name<<endl;
			return ;
		}
		//����Ǿ���·��
		//pass
		
		
		//���ֻ�ǵ�ǰĿ¼
		//ɾ��diritem��λͼ����顢������    inodetable ������� ���ݲ��������//�ɱ�����  �ɱ����Ǽ��� Ŀ¼���濴��������
		
		
		int inodeNum=fileItem->inodeNum;
		int group=inodeNum/GROUP_INODE_NUM;
		//���inodeMap
		releaseInodeMap(inodeNum);
		
		//���blockMap
		releaseBlockMap(inodeNum);
		
		//��顢������ free����
		Inode* file=(Inode*)getIndoeAddr(inodeNum);
		updateSpAndBg(file->size,group,-1);
		
		//dirItem�����Ľ�����Ȼ��curDirInode len--
		DirItem* last=(DirItem*)(getFreeDir()-sizeof(DirItem));
		fileItem->inodeNum=last->inodeNum;
		fileItem->type=last->type;
		strcpy(fileItem->name,last->name);
		
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		cur->len--;
		cout<<"delete "<<name<<" successfully"<<endl;
	}
	
	//����
	void rmfile(char *name){
		
		DirItem* fileItem=(DirItem*)isExist(name,0);
		if(fileItem==NULL){
			cout<<"Error: not found "<<name<<endl;
			return ;
		}
		//����Ǿ���·��
		//pass
		
		
		//���ֻ�ǵ�ǰĿ¼
		//ɾ��diritem��λͼ����顢������    inodetable ������� ���ݲ��������//�ɱ�����  �ɱ����Ǽ��� Ŀ¼���濴��������
		
		
		int inodeNum=fileItem->inodeNum;
		int group=inodeNum/GROUP_INODE_NUM;
		//���inodeMap
		releaseInodeMap(inodeNum);
		
		//���blockMap
		releaseBlockMap(inodeNum);
		
		//��顢������ free����
		Inode* file=(Inode*)getIndoeAddr(inodeNum);
		updateSpAndBg(file->size,group,-1);
		
		//dirItem�����Ľ�����Ȼ��curDirInode len--
		DirItem* last=(DirItem*)(getFreeDir()-sizeof(DirItem));
		fileItem->inodeNum=last->inodeNum;
		fileItem->type=last->type;
		strcpy(fileItem->name,last->name);
		
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		cur->len--;
		cout<<"delete "<<name<<" successfully"<<endl;
	}
	
	//��֧�ֵ�ǰĿ¼��
	void* open(char *name){
		
		
		//�ҵ�dirItem��Ӧ��λ��
		Inode* cur=getIndoeAddr(curDirInode);//�Ƿ�ֱ�Ӽ�¼��ַЧ�ʸ��ߣ�
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==0){	
				cout<<"The file is open at"<<(void*)tmp<<endl;
				return getIndoeAddr(tmp->inodeNum);	//�ҵ�inode  //����ζ���ҵ�block
			}
		}
		
		//����Ҳ���:���������Ϣ
		cout<<"Not found\n"<<endl;
		return NULL;
			
	}
	
	//��֧�ֵ�ǰĿ¼��
	//��Ϊ�п��ܶ�������ǰ�Ķ�����������Ҫע���β�ĵط�
	void read(char *name){
		Inode* file=(Inode*)open(name);
		
		//�ҵ������ļ�
		if(file==NULL){
			cout<<"Not found\n"<<endl;
			return ;
		}
		
		//����ļ�������'\0'ֹͣ
		char* tmp;
		int cnt=0;
		for (int i=0;i<file->len;++i){
		    tmp=(char*)getBlockAddr(file->block[cnt++]);
			cout<<tmp;			
		}
		cout<<endl;
		cout<<"Read over\n";
		
	}
	
	//��֧�ֵ�ǰĿ¼��,��֧��д�룬��֧��append
	void write(char *name,char *content){
		Inode* file=(Inode*)open(name);
		
		//�ҵ������ļ�
		if(file==NULL){
			cout<<"Not found\n"<<endl;
			return ;
		}
		
		
		//��֧��д�룬��֧��append
		//��ȡд�볤��
		file->len=strlen(content)/1024;
		if((strlen(content)%1024!=0)){
			file->len++;
		}
		
		//д�뵽block��
		for(int i=0;i<file->len;++i){
			char *tmp=(char*)getBlockAddr(file->block[i]);
			if(i==file->len-1){
				strncpy(tmp,content+i*1024,strlen(content)%1024);
				break;
			}
			strncpy(tmp,content+i*1024,1024);
		}
		
		cout<<"Write over."<<endl;
			
	}
	
	//��֧�ֵ�ǰĿ¼��
	void ls(){
		//����
		printf("name\t\t type\t\t size\t\t inode\t\t content_size\t\t\n");
		
		//���ÿһ��Ŀ¼�������:	
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;

		for(int i=0;i<cur->len;++i){
			//cout<<(getBlockAddr(curDirBlock)+sizeof(DirItem)*i)<<endl;
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			Inode* inode=getIndoeAddr(tmp->inodeNum);
			printf("%s\t\t %d\t\t %d\t\t %d\t\t %d\t\t\n",tmp->name,tmp->type,inode->size,tmp->inodeNum,inode->len);
		}	
	}
	
	
	
};


class FileSystem{
private:
	char* type="EXT2";
	Disk *disk;
public:
	FileSystem(){disk=new Disk();cout<<"create fileSystem...\n";}//��Ҫע����ǳ�ʼ���Ľ׶�
	FileSystem(int diskSize){disk=new Disk(diskSize);cout<<"create fileSystem...\n";};
	void makeDir(char* path){cout<<"mkdir..."<<endl;disk->mkdir(path);};
	void delDir(char* dirPath){cout<<"deldir..."<<endl;disk->rmdir(dirPath);};
	void renameFile(char* path,char* new_name){cout<<"rename..."<<endl;disk->mv(path,new_name);};
	void openFile(char* path){cout<<"open..."<<endl;disk->open(path);};
	void touchFile(char* path){cout<<"touch..."<<endl;disk->touch(path);};
	void delFile(char* path){cout<<"delFile..."<<endl;disk->rmfile(path);};
	void showFile(){cout<<"ls..."<<endl;disk->ls();};
	void cdDir(char* name){cout<<"cd..."<<endl;disk->cd(name);}
	void write(char* name,char* content){cout<<"write..."<<endl;disk->write(name,content);}
	void read(char* name){cout<<"read..."<<endl;disk->read(name);}
	void pwd(){cout<<"pwd..."<<endl;disk->pwd();}
	
	
	~FileSystem(){delete disk;}
};





//shell0
int main(){
	//init
	FileSystem fileSystem=FileSystem();//MBΪ��λ
	while(true){
		cout<<"input your op:"<<endl;
		string op;
		cin>>op;
		if(op=="mkdir"){
			char name[24];
			cin>>name;
			fileSystem.makeDir(name);
		}else if(op=="deldir"){
			char name[24];
			cin>>name;
			fileSystem.delDir(name);
			
		}else if(op=="mv"){
			char oldName[24],newName[24];
			cin>>oldName>>newName;
			fileSystem.renameFile(oldName,newName);
		}else if(op=="touch"){
			char name[24];
			cin>>name;
			fileSystem.touchFile(name);
		}else if(op=="ls"){
			fileSystem.showFile();
		}else if(op=="delfile"){
			char name[24];
			cin>>name;
			fileSystem.delFile(name);
			
		}else if(op=="open"){
			char name[24];
			cin>>name;
			fileSystem.openFile(name);
		    	
		}else if(op=="read"){
			char name[24];
			cin>>name;
			fileSystem.read(name);
			
		}else if(op=="write"){
			char name[24];
			char content[1024];
			cin>>name>>content;
			fileSystem.write(name,content);
			
		}else if(op=="cd"){
			char name[24];
			cin>>name;
			fileSystem.cdDir(name);
		}else if(op=="pwd"){
			fileSystem.pwd();
		}else if(op=="exit"){
			cout<<"exit system...\n";
			break;
		}	
	}	
}



