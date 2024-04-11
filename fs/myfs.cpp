#include "myfs.h"
#include <string.h>
#include <iostream>
#include <sstream>

const char *MyFs::MYFS_MAGIC = "DaLI";

MyFs::MyFs(BlockDeviceSimulator *blkdevsim_)
	: blkdevsim(blkdevsim_)
{
	struct myfs_header header;
	blkdevsim->read(0, sizeof(header), (char *)&header);

	if (strncmp(header.magic, MYFS_MAGIC, sizeof(header.magic)) != 0 ||
		(header.version != CURR_VERSION))
	{
		std::cout << "Did not find myfs instance on blkdev" << std::endl;
		std::cout << "Creating..." << std::endl;
		format();
		std::cout << "Finished!" << std::endl;
	}
}

void MyFs::format()
{
	// put the header in place
	struct myfs_header header;
	strncpy(header.magic, MYFS_MAGIC, sizeof(header.magic));
	header.version = CURR_VERSION;
	blkdevsim->write(0, sizeof(header), (const char *)&header);

	// create inode table:
	char zeros[INODE_TABLE_SIZE] = {0};
	std::cout << "Inode table size: " << INODE_TABLE_SIZE << std::endl;
	std::cout << "Inode amount: " << INODE_AMOUNT << std::endl;
	std::cout << "Inode size: " << sizeof(inode_entry) << std::endl;
	blkdevsim->write(INODE_TABLE_START, INODE_TABLE_SIZE, zeros);

	// base directory:
	inode_entry basedir_inode = {0};
	basedir_inode.is_dir = true;
	basedir_inode.size = sizeof(dir_entry) * INIT_DIR_SIZE;
	basedir_inode.addr = MEM_START;
	setInode(BASEDIR_INODE_INDEX, basedir_inode); // first inode
	blkdevsim->write(basedir_inode.addr, basedir_inode.size, zeros);
}

void MyFs::create_file(std::string path_str, bool directory)
{
	inode_entry entry;
	entry.addr = UINT32_MAX;
	entry.size = 0;
	entry.is_dir = directory;

	const std::string &parent_dir_path = removeFileNameFromPath(path_str);
	uint8_t newInodeIndex = findFirstFreeInode();

	addToDirectory(getInodeIndexFromPath(parent_dir_path), newInodeIndex, getFileNameFromPath(path_str)); // do this before setting inode, so if a runtime error occures the operation will stop

	setInode(newInodeIndex, entry);
}

std::string MyFs::get_content(std::string path_str)
{
	inode_entry file_inode;
	getInode(getInodeIndexFromPath(path_str), file_inode);
	if (file_inode.is_dir)
	{
		throw std::runtime_error("\"" + path_str + "\" is a directory");
	}
	if (file_inode.addr == UINT32_MAX) // not allocated - empty file
	{
		return "";
	}

	std::unique_ptr<char[]> buffer(new char[file_inode.size]{0}); // smart pointer so i dont have to free later
	blkdevsim->read(file_inode.addr, file_inode.size, &buffer[0]);
	return std::string(&buffer[0], file_inode.size);
}

void MyFs::set_content(std::string path_str, std::string content)
{
	inode_entry file_inode;
	uint8_t inode_index = getInodeIndexFromPath(path_str);
	getInode(inode_index, file_inode);
	// std::cout << "setting content to inode " << (int)inode_index << " at previous address " << (int)file_inode.addr << std::endl;
	if (file_inode.is_dir)
	{
		throw std::runtime_error("\"" + path_str + "\" is a directory");
	}

	file_inode.size = content.size();
	file_inode.addr = findFreeSpace(file_inode.size);
	setInode(inode_index, file_inode);
	blkdevsim->write(file_inode.addr, file_inode.size, content.c_str());
}

MyFs::dir_list MyFs::list_dir(std::string path_str)
{
	dir_list ans;
	inode_entry dir_inode;
	getInode(getInodeIndexFromPath(path_str), dir_inode);
	if (!dir_inode.is_dir)
	{
		throw std::runtime_error(path_str + " is a file, not a directory");
	}
	for (uint32_t i = dir_inode.addr; i < dir_inode.addr + dir_inode.size; i += sizeof(dir_entry))
	{
		dir_entry curr;
		blkdevsim->read(i, sizeof(dir_entry), (char *)&curr);
		if (curr.inode_index != 0) // valid dir entry
		{
			inode_entry inode;
			dir_list_entry list_entry;
			getInode(curr.inode_index, inode);
			list_entry.is_dir = inode.is_dir;
			list_entry.file_size = inode.size;
			list_entry.name = std::string(curr.name, NAME_SIZE);

			ans.push_back(list_entry);
		}
	}
	return ans;
}

void MyFs::getInode(uint8_t index, inode_entry &out) const
{
	blkdevsim->read(INODE_TABLE_START + index * sizeof(inode_entry), sizeof(inode_entry), (char *)&out);
}

void MyFs::setInode(uint8_t index, const inode_entry &inode) const
{
	blkdevsim->write(INODE_TABLE_START + index * sizeof(inode_entry), sizeof(inode_entry), (const char *)&inode);
}

void MyFs::addToDirectory(const uint32_t dir_inode_index, const uint8_t inode_index, const std::string &filename) const
{
	inode_entry dir;
	getInode(dir_inode_index, dir);
	if (!dir.is_dir)
	{
		throw std::runtime_error("specifed path is a file, not a directory");
	}
	if (dir.addr == UINT32_MAX)
	{
		reallocInode(dir, dir_inode_index, INIT_DIR_SIZE * sizeof(dir_entry));
		// throw std::runtime_error("directory not created yet");
	}

	// create the dir entry to add
	dir_entry entry_to_add;
	entry_to_add.inode_index = inode_index;
	strncpy(entry_to_add.name, filename.c_str(), NAME_SIZE);
	entry_to_add.name[NAME_SIZE] = 0;

	bool hasAdded = false; // we need to iterate through the whole directory to check for name duplication
	while (!hasAdded)
	{
		for (uint32_t i = dir.addr; i < dir.addr + dir.size; i += sizeof(dir_entry))
		{
			dir_entry curr;
			blkdevsim->read(i, sizeof(dir_entry), (char *)&curr);
			if (strncmp(curr.name, filename.c_str(), NAME_SIZE) == 0)
			{
				throw std::runtime_error("file/directory \"" + filename + "\" already exists");
			}
			if (!hasAdded && curr.inode_index == 0) // empty entry
			{
				blkdevsim->write(i, sizeof(dir_entry), (const char *)&entry_to_add);
				hasAdded = true;
			}
		}
		if (!hasAdded)
		{
			// add to the directory INIT_DIR_SIZE more entries
			uint32_t new_size = dir.size + INIT_DIR_SIZE * sizeof(dir_entry);
			reallocInode(dir, dir_inode_index, new_size);
			std::cout << "Directory full, reallocating it with new size of " << (unsigned int)new_size << " (" << new_size / sizeof(dir_entry) << " entries)" << std::endl;
		}
	}
}

void MyFs::reallocInode(inode_entry &dir, uint8_t inode_index, uint32_t new_size) const
{
	std::unique_ptr<char[]> buffer(new char[new_size]{0});
	if (dir.addr != UINT32_MAX)
	{
		// take what was before to the new reallocated memory
		blkdevsim->read(dir.addr, dir.size, &buffer[0]);
	}
	dir.addr = INT32_MAX; // free the memory
	dir.size = new_size;
	setInode(inode_index, dir);
	dir.addr = findFreeSpace(new_size); // realloc after freeing
	setInode(inode_index, dir);
	blkdevsim->write(dir.addr, dir.size, &buffer[0]);
}

std::deque<std::string> MyFs::splitPath(const std::string &path) const
{
	if (path.find(' ') != std::string::npos)
	{
		throw std::runtime_error("path contains whitespaces");
	}
	std::deque<std::string> res;
	std::stringstream ss(path);
	std::string curr;

	while (std::getline(ss, curr, PATH_DELIMITER))
	{
		if (!curr.empty() && curr != ".")
		{
			res.push_back(capFileName(curr));
		}
	}

	return res;
}

uint8_t MyFs::getInodeIndexFromPath(std::string path) const
{
	if (path.empty() || path == ".")
	{
		return BASEDIR_INODE_INDEX;
	}
	if (path.find(PATH_DELIMITER) == std::string::npos) // no slashes - path is only name in the basedir
	{
		inode_entry curr_dir;
		getInode(BASEDIR_INODE_INDEX, curr_dir);
		return findFileInDirectory(curr_dir.addr, curr_dir.size, capFileName(path));
	}

	uint8_t dir_index = BASEDIR_INODE_INDEX;
	const auto &dir_names = splitPath(path);

	for (const auto &name : dir_names)
	{
		// std::cout << "dir index - " << (unsigned int)dir_index << std::endl;
		inode_entry curr_dir;
		getInode(dir_index, curr_dir);
		if (!curr_dir.is_dir && name != getFileNameFromPath(path))
		{
			throw std::runtime_error("parent directory of \"" + name + "\" is a file, not a directory");
		}
		dir_index = findFileInDirectory(curr_dir.addr, curr_dir.size, name);
	}

	return dir_index;
}

std::string MyFs::removeFileNameFromPath(const std::string &path) const
{
	if (path.find(PATH_DELIMITER) == std::string::npos)
	{
		return "";
	}
	return path.substr(0, path.find_last_of(PATH_DELIMITER));
}

std::string MyFs::getFileNameFromPath(const std::string &path) const
{
	if (path.find(PATH_DELIMITER) == std::string::npos)
	{
		return capFileName(path);
	}
	return capFileName(path.substr(path.find_last_of(PATH_DELIMITER) + 1));
}

std::string MyFs::capFileName(const std::string &filename) const
{
	if (filename.size() > NAME_SIZE)
	{
		const auto &newfilename = filename.substr(0, NAME_SIZE);
		std::cout << "file/directory name is too long (max is " << (unsigned int)NAME_SIZE
				  << "), converting it to " << newfilename << std::endl;
		return newfilename;
	}
	return filename;
}

uint8_t MyFs::findFileInDirectory(uint32_t dir_addr, uint32_t dir_size, const std::string &filename) const
{
	for (uint32_t i = dir_addr; i < dir_addr + dir_size; i += sizeof(dir_entry))
	{
		dir_entry curr;
		blkdevsim->read(i, sizeof(dir_entry), (char *)&curr);
		if (strncmp(curr.name, filename.c_str(), NAME_SIZE) == 0)
		{
			return curr.inode_index;
		}
	}
	throw std::runtime_error("file/directory \"" + filename + "\" not found");
}

uint8_t MyFs::findFirstFreeInode() const
{
	inode_entry inode;
	for (uint8_t i = 0; i < INODE_AMOUNT; i++)
	{
		getInode(i, inode);
		if (inode.addr == 0) // inode is free
		{
			return i;
		}
	}
	throw std::runtime_error("no free inodes");
}

uint32_t MyFs::findFreeSpace(uint32_t wantedSize) const
{
	std::set<inode_entry> inodes;
	for (uint8_t i = 0; i < INODE_AMOUNT; i++)
	{
		inode_entry currInode;
		getInode(i, currInode);
		if (currInode.addr != 0 && currInode.addr != UINT32_MAX) // allocated inode
		{
			// std::cout << (unsigned int)i << ". addr - " << (unsigned int)currInode.addr << ", size - " << (unsigned int)currInode.size << std::endl;
			inodes.insert(currInode);
		}
	}
	// std::cout << "allocated inodes count - " << inodes.size() << std::endl;

	uint32_t min = UINT32_MAX;
	uint32_t minAddress = UINT32_MAX;
	uint32_t lastFreeSpaceAddress = MEM_START;
	uint32_t freeSpace = 0;
	for (auto inode = inodes.begin(); inode != inodes.end(); ++inode) // starts from the second inode
	{
		freeSpace = inode->addr - lastFreeSpaceAddress;
		// std::cout << "current free space check - " << freeSpace << std::endl;
		if (freeSpace >= wantedSize && freeSpace < min)
		{
			min = freeSpace;
			minAddress = lastFreeSpaceAddress;
		}

		lastFreeSpaceAddress = inode->addr + inode->size + 1;
	}

	// check the space from the last to the end
	freeSpace = blkdevsim->DEVICE_SIZE - lastFreeSpaceAddress;
	if (freeSpace >= wantedSize && freeSpace < min)
	{
		min = freeSpace;
		minAddress = lastFreeSpaceAddress;
	}

	if (min == UINT32_MAX)
	{
		throw std::runtime_error("no free space");
	}
	// std::cout << "address = " << minAddress << std::endl;
	return minAddress;
}
