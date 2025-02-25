﻿#include <geekos/oscourse/dirmgmt.h>
#include <geekos/malloc.h>
#include <geekos/oscourse/oft.h>

// We assume we have two open functions
// One opens file in some path, other opens file pointed to by the Inode
// inodeNum = -1 if file does not exist

int Create_Path(char *path, Path *path_obj) {
	Print("Entering create path\n");
	path_obj->childPath = NULL;
	
	strcpy(path_obj->fname, "root");
	
	char tmpPath[MAX_PATH_LENGTH];
	strcpy(tmpPath, path);
	
	Path* curNode = path_obj;
	int index = 0;
	while(true)
	{
		//~ Print("Ptr val %d path is %s\n", (int)(path_obj->childPath), path);
		if(tmpPath[index] == '\0' || (tmpPath[index] == '/' && tmpPath[index +1 ] == '\0'))
		{
			//~ Print("Breaking\n");
			
			break;
		}
		else
		{
			curNode->childPath = (Path*)Malloc(sizeof(Path));
			curNode = curNode->childPath;
			curNode->childPath = NULL;
			int next_slash = index + 1;
			while(true)
			{
				if(tmpPath[next_slash] == '/' || tmpPath[next_slash] == '\0')
				{
					break;
				}                                
				next_slash++;
			}
			char tmpRep = tmpPath[next_slash];
			tmpPath[next_slash] = '\0';
			strcpy(curNode->fname, &tmpPath[index + 1]);
			tmpPath[next_slash] = tmpRep;        
			index = next_slash;
		}
	}
	return 0;		
}

int Free_Path(Path *path_obj) {
	if(path_obj->childPath != NULL)
	{
		Free_Path(path_obj->childPath);
		Free(path_obj);
	}
	return 0;
}


// If file does not exist, inode num returned is -1, error code is 0
int Exists_File_Name(Inode* pwd, const char *fileName, int *inodeNum ,int *entryNum)
{
	//int *f = Open(pwd);
	DirHeader dhead;
	//int numread;
	int rc;
	*inodeNum = -1;
	int curEntryNum = -1;
	*entryNum = -1;
	
	//numread = readBytes(f, &dhead, sizeof(dhead));
	int read = 0;
	rc = My_ReadLow(pwd, (char*)&dhead, read, sizeof(dhead));
	read += sizeof(dhead);
	//Print("Dehead attr %d %d\n", dhead.numEntries, dhead.parentInode);
	//~ Print("PWD name %s\n", pwd->meta_data.filename);
	if(rc != 0)
	{
		Print("Exists_File_Name: Didn't work My_ReadLow\n");
		return -1; 
	}
	while(curEntryNum + 1 < dhead.numEntries)
	{
		//Print("CHAP %d", read);
		struct DirEntry tmp;
		rc = My_ReadLow(pwd, (char*)&tmp, read, sizeof(tmp));
		//~ Print("name read %s\n", tmp.fname);
		read += sizeof(tmp);
		if(rc != 0)
		{
			Print("Exists_File_Name: Didn't work My_ReadLow after the 1st time\n");
			return -1;
		}
		curEntryNum++;
		if(strcmp(tmp.fname, fileName) == 0)
		{
			*inodeNum = tmp.inode_num;
			*entryNum = curEntryNum;
			return 0;
		}
	}
	Print("File not found\n");
	return -1;
}


// If path does not exist: -1
int Get_Inode_From_Path(Path path, int* inode_num)
{
	Print("entering inode from path\n");
	int rc;
	Inode* curInode;
	int curInodeNum = 0;
	Get_Inode_Into_Cache(0, &curInode);
	//~ Print("perm %d\n", curInode->meta_data.permissions);
	//~ Print("-1\n");
	if(Check_Perms(1, curInode) != 0) {
		Print("Get_Inode_From_Path: Permission check failed \n");
		Unfix_Inode_From_Cache(0);
		return -1;
	}

	Path* curPath = path.childPath;
	while(true)
	{
		
		//~ Print("0\n");
		if(curPath == NULL)
		{
			//~ Print("1\n");
			Unfix_Inode_From_Cache(curInodeNum);
			//~ Print("2\n");
			*(inode_num) = curInodeNum;
			Print("inum %d\n", curInodeNum);
			//~ Print("3\n");
			//~ Print("%s %d\n", path.fname, (int)path.childPath);
			return 0;
		} 
		else
		{
			Print("Here");
			if(!(curInode->meta_data.is_directory))
			{
				Unfix_Inode_From_Cache(curInodeNum);
				Print("Arbit file in path name\n");
				return -1;
			}
			int newInodeNum;
			int entryNum; // Not Used Here
			rc = Exists_File_Name(curInode, curPath->fname, &newInodeNum, &entryNum);
			Unfix_Inode_From_Cache(curInodeNum);
			if(rc != 0)//on error inode** is garbage
			{
				Print("Get_Inode_From_Path:Exists_File_Name failed (pain in Exists_File_Name) \n");
				return -1;
			}
			
			curInodeNum = newInodeNum;
			Get_Inode_Into_Cache(curInodeNum, &curInode);
			if(Check_Perms(1, curInode) != 0) {
				Print("Get_Inode_From_Path: Permission check failed \n");
				Unfix_Inode_From_Cache(curInodeNum);
				return -1;
			}
			curPath = curPath->childPath;
		}
	}
}
                
int Make_Dir_With_Inode(Inode *parentInode, char* fileName, int pinodeNum)
{
	// checking whether file already exists
	int inodeNum;
	int entryNum; // Not Used Here
	int rc = Exists_File_Name(parentInode, fileName, &inodeNum, &entryNum);
	if( rc == 0) {
		Print("Make_Dir: Dir/File already exists \n");
		return rc;
	}
	
	// Basically creating a new file
	int newInodeNum;
	InodeMetaData meta_data;
	strcpy((char*)(&meta_data.filename),fileName); // assuming filename is null terminated
	meta_data.group_id = CURRENT_THREAD->group_id;
	meta_data.owner_id = CURRENT_THREAD->user_id;
	meta_data.is_directory = 0x1;
	meta_data.file_size = 0; // check if size of works
	meta_data.permissions = 0x7; //Set them permissions
	meta_data.permissions = meta_data.permissions | (7 << 3);
	meta_data.permissions = meta_data.permissions | (3 << 6);
	rc = Create_New_Inode(meta_data, &newInodeNum);
	if( rc != 0 ) {
		Print("Create_New_Inode failed in Make_Dir oh No!\n");
		return rc;
	}
	
	DirHeader dheadnew;
	Allocate_Upto(newInodeNum, sizeof(DirHeader));
	dheadnew.parentInode = pinodeNum;
	dheadnew.numEntries = 0;
	
	Inode* tmp;
	rc = Get_Inode_Into_Cache(newInodeNum, &tmp);
	
	rc = My_WriteLow(tmp, (char*)&dheadnew, 0, sizeof(DirHeader)) | rc;

	rc = rc | Unfix_Inode_From_Cache(newInodeNum);
	Print("Put new header\n");
	if(rc)
	{
		Print("Error putting new dir header\n");
		return rc;
	}
	
	
	// Update numEntries in the header
	DirHeader dhead;
	
	rc = My_ReadLow(parentInode, (char*)&dhead, 0, sizeof(dhead));
	
	
	if(rc != 0)
	{
		//Error
		Print("Make_Dir: reading directory header failed\n");
		return -1;
	}
	dhead.numEntries++;	
	My_WriteLow(parentInode, (char*)&dhead, 0, sizeof(dhead));
	
	
	//Add new entry
	DirEntry newEntry;
	strcpy((char*)&newEntry.fname,fileName); // assuming filename is null terminated
	newEntry.inode_num = newInodeNum;
	int oldSize = (parentInode->meta_data).file_size;
	rc = Allocate_Upto(pinodeNum , oldSize + sizeof(newEntry));
	if(rc) 
	{
		Print("Failure allocating space for dir entry");
		
		return rc;
	}
		
	rc = My_WriteLow(parentInode, (char*)&newEntry, oldSize, sizeof(newEntry));
	return rc;
}

int Make_Dir(char* parentPath, char* fileName)
{
	Path pPath;
	Create_Path(parentPath, &pPath);
	int inodeNum;
	int rc = Get_Inode_From_Path(pPath, &inodeNum);
	if( rc != 0 || inodeNum == -1)
	{        
		Print("Make_Dir: error or path doesn't exist\n");
		return -1;
		// Error, or parent path does not exist
	}
	Inode* parentInode ;
	rc = Get_Inode_Into_Cache(inodeNum, &parentInode);
	if(rc)
	{
		Print("dirmgmt.c/Make_Dir: Couldnt get directory inode into cache\n");
		return -1;
	}
	if(Check_Perms(2, parentInode) != 0) {
		Print("Make_Dir: Permission check failed \n");
		Unfix_Inode_From_Cache(inodeNum);
		return -1;
	}
	rc = Make_Dir_With_Inode(parentInode,fileName, inodeNum);
	if(rc)
	{
		Print("dirmgmt/Make_Dir: Couldnot make dir");
	}
	rc = rc | Unfix_Inode_From_Cache(inodeNum);
	return rc;
} 

int Remove_Dir_With_Inode(Inode *parentInode, char *fileName, int pinodeNum)//Assumption Only Empty Directories
{
	int inodeNum;
	int entryNum;
    int rc = Exists_File_Name(parentInode,fileName,&inodeNum,&entryNum);
    if( rc != 0)
    {
    	Print("Remove_Dir:File Exists failed\n");
		return -1;
	}
		    
	
	// TODO: If the file is a directory. We need to check whether the directory is actually empty
	//if(inodeNum ) Check if the dir is empty
	Inode *delInode;
	rc = Get_Inode_Into_Cache(inodeNum, &delInode);
	if(rc || (((delInode->meta_data).is_directory) && (delInode->meta_data).file_size != sizeof(DirHeader)) ){
		Print("Remove_Dir: Not directory, or not empty or get into cache error\n");
		return -1;
	}
	rc = Unfix_Inode_From_Cache(inodeNum);
	rc = rc | Free_Inode(inodeNum);
	if(rc)
	{
		Print("Remove_Dir: Could not free inode");
		return rc;
	}
	
	// Removing The Entry
	DirHeader dhead;
	rc = My_ReadLow(parentInode, (char*)&dhead, 0, sizeof(dhead));
	
	if(rc != 0)
	{
		//Error
		Print("Make_Dir: reading inode failed\n");
		return -1;
	}
	dhead.numEntries--;
	rc =  My_WriteLow(parentInode, (char*)&dhead, 0, sizeof(dhead));
	
	DirEntry lastEntry;
	if(dhead.numEntries > 1)
	{
		rc = My_ReadLow(parentInode, (char*)&lastEntry, (dhead.numEntries)*sizeof(DirEntry), sizeof(DirEntry));
		int offset = sizeof(DirHeader) + entryNum*sizeof(DirEntry);
		My_WriteLow(parentInode, (char*)&lastEntry, offset, sizeof(DirEntry));
	}
	
	// remove the last entry in the file
	rc = Truncate_From(pinodeNum, sizeof(DirHeader) + dhead.numEntries*sizeof(DirEntry));
	if(rc)
	{
		Print("remodeDir: Error while trucating directory\n");
		return rc;
	}
	
	return 0;
}

int Remove_Dir( char* parentPath, char* fileName)
{
	Path pPath;
	Create_Path(parentPath, &pPath);
	Inode *parentInode;
	int inodeNum;
	int rc = Get_Inode_From_Path(pPath, &inodeNum);
	if(rc != 0 || inodeNum == -1)
	{        
		Print("Remove_Dir: Path Doesn't Exist! \n");
		return rc;
		// Error, or parent path does not exist
	}
	rc = Get_Inode_Into_Cache(inodeNum, &parentInode);
	if(rc)
	{
		Print("Remove_Dir: could not get inode into cache\n");
		return rc;
	}
	if(Check_Perms(2, parentInode) != 0) {
		Print("Remove_Dir: Permission check failed \n");
		Unfix_Inode_From_Cache(inodeNum);
		return -1;
	}
	rc = Remove_Dir_With_Inode(parentInode,fileName, inodeNum);
	rc = rc | Unfix_Inode_From_Cache(inodeNum);
	if(rc)
	{
		Print("Remove_Dir: Could not remove directory\n");
		return rc;
	}
	return 0;
}
