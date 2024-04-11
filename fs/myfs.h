#ifndef __MYFS_H__
#define __MYFS_H__

#include <memory>
#include <vector>
#include <stdint.h>
#include "blkdev.h"
#include <iostream>

#include <array>
#include <set>
#include <deque>

class MyFs
{
public:
	MyFs(BlockDeviceSimulator *blkdevsim_);

	/**
	 * dir_list_entry struct
	 * This struct is used by list_dir method to return directory entry
	 * information.
	 */
	struct dir_list_entry
	{
		/**
		 * The directory entry name
		 */
		std::string name;

		/**
		 * whether the entry is a file or a directory
		 */
		bool is_dir;

		/**
		 * File size
		 */
		int file_size;
	};
	typedef std::vector<struct dir_list_entry> dir_list;

	/**
	 * format method
	 * This function discards the current content in the blockdevice and
	 * create a fresh new MYFS instance in the blockdevice.
	 */
	void format();

	/**
	 * create_file method
	 * Creates a new file in the required path.
	 * @param path_str the file path (e.g. "/newfile")
	 * @param directory boolean indicating whether this is a file or directory
	 */
	void create_file(std::string path_str, bool directory);

	/**
	 * get_content method
	 * Returns the whole content of the file indicated by path_str param.
	 * Note: this method assumes path_str refers to a file and not a
	 * directory.
	 * @param path_str the file path (e.g. "/somefile")
	 * @return the content of the file
	 */
	std::string get_content(std::string path_str);

	/**
	 * set_content method
	 * Sets the whole content of the file indicated by path_str param.
	 * Note: this method assumes path_str refers to a file and not a
	 * directory.
	 * @param path_str the file path (e.g. "/somefile")
	 * @param content the file content string
	 */
	void set_content(std::string path_str, std::string content);

	/**
	 * list_dir method
	 * Returns a list of a files in a directory.
	 * Note: this method assumes path_str refers to a directory and not a
	 * file.
	 * @param path_str the file path (e.g. "/somedir")
	 * @return a vector of dir_list_entry structures, one for each file in
	 *	the directory.
	 */
	dir_list list_dir(std::string path_str);

private:
	/**
	 * This struct represents the first bytes of a myfs filesystem.
	 * It holds some magic characters and a number indicating the version.
	 * Upon class construction, the magic and the header are tested - if
	 * they both exist than the file is assumed to contain a valid myfs
	 * instance. Otherwise, the blockdevice is formated and a new instance is
	 * created.
	 */
	struct myfs_header
	{
		char magic[4];
		uint8_t version;
	};

	BlockDeviceSimulator *blkdevsim;

	static const uint8_t CURR_VERSION = 0x03;
	static const char *MYFS_MAGIC;
	static const uint8_t NAME_SIZE = 10;
	static const uint8_t INIT_DIR_SIZE = 4;

	static const uint8_t BASEDIR_INODE_INDEX = 0;

	typedef struct inode_entry
	{
		// addr is equal to 0 if inode is not defined, UINT32_MAX if defined but not allocated (size 0)
		uint32_t addr;
		uint32_t size;
		bool is_dir;

		// if we want to use it in a set
		bool operator<(const inode_entry &other) const
		{
			// i was stuck for an half hour on this line not figuring out that i compared addr to other.size :(((
			// std::cout << "this addr - " << addr << ", other addr - " << other.addr << " res - " << (int)(addr < other.size) << std::endl;
			return addr < other.addr;
		}
	} inode_entry;

	typedef struct dir_entry
	{
		uint8_t inode_index;
		char name[NAME_SIZE];
	} dir_entry;

	// same way that linux calculates it, one inode per 16KB. (equals to 64 with DEVICE_SIZE = 1024*1024)
	static const int INODE_AMOUNT = BlockDeviceSimulator::DEVICE_SIZE / 16384;
	static const int INODE_TABLE_SIZE = INODE_AMOUNT * sizeof(inode_entry);
	static const int INODE_TABLE_START = sizeof(myfs_header) + 1;
	static const int MEM_START = INODE_TABLE_START + INODE_TABLE_SIZE + 1;

	static const char PATH_DELIMITER = '/';

	void getInode(uint8_t index, inode_entry &out) const;
	void setInode(uint8_t index, const inode_entry &inode) const;

	std::deque<std::string> splitPath(const std::string &path) const;
	std::string removeFileNameFromPath(const std::string &path) const;
	std::string getFileNameFromPath(const std::string &path) const;
	std::string capFileName(const std::string &filename) const;

	uint8_t getInodeIndexFromPath(std::string path) const;

	void addToDirectory(const uint32_t dir_inode_index, const uint8_t inode_index, const std::string &filename) const;
	void reallocInode(inode_entry &dir, uint8_t inode_index, uint32_t new_size) const;
	uint8_t findFileInDirectory(uint32_t dir_addr, uint32_t dir_size, const std::string &filename) const; // returns inode index

	uint8_t findFirstFreeInode() const;
	uint32_t findFreeSpace(uint32_t wantedSize) const;
};

#endif //__MYFS_H__
