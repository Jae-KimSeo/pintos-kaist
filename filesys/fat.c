#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

#define SIZE_MAX (18446744073709551615UL)
#define BITMAP_ERROR SIZE_MAX

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;
struct bitmap * fat_bitmap;

//bitmap_scan_and_flip(fat_bitmap, 0, 1, false) + 1
//
cluster_t get_empty_cluster() {
	size_t clst = bitmap_scan_and_flip(fat_bitmap, 0, 1, false) + 1;
	if (clst == BITMAP_ERROR)
		return 0;
	else
		return (cluster_t) clst;
}

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();

	fat_bitmap = bitmap_create(fat_fs->fat_length);
}

void init_fat_bitmap(void){
	for(int clst = 0; clst < fat_fs->fat_length; clst++){
		// FAT occupied with EOC or any value (next cluster)
		if(fat_fs->fat[clst])
			bitmap_set(fat_bitmap, clst, true);
	}
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	//여기서 fat_size_in_bytes는 table 배열의 총 byte
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		//bytes_left = fat_size_in_bytes - bytes_read 
		//
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			// disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce)
			// 섹터 크기만큼 읽어온다. 
			// fat_start 에서 
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	//boot를 create하고 
	fat_fs_init ();
	//fat_fs_init 
	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	//fat_length의 data_start필드를 초기화 해야한다.
	//fat_fs, fat_length : 파일 시스템에 얼마나 많은 클러스터가 있는지 저장하고
	//파일 data_start 저장을 시작할 수 있는 섹터를 저장한다.
	//fat_fs는 전역
	//fat_boot는 이미 초기화되어있다.
	//
	fat_fs->fat_length = disk_size(filesys_disk) - fat_fs->data_start;
	//fat는 하나의 테이블 
	//sector의 index값과 value를 가진다.
	fat_fs->data_start = fat_fs->bs.fat_start+fat_fs->bs.fat_sectors;
	//fat_fs->bs.fat_start+fat_fs->bs.fat_sectors
	//fat_fs, fat_sectors
	//fat의 시작, fat_fs->bs.fat_start+fat_fs->bs.fat_sectors 
	//fat_start, fat_sectors 
	fat_fs->last_clst = fat_fs->bs.total_sectors-1;
	//last_clst는 전체 섹터 에서 1을 뺀 값이다.
	//fat_fs->bs.total_sectors-1
	lock_init(&fat_fs->write_lock);

}

cluster_t fat_create_chain(cluster_t clst){
	//clst is equal to zero
	//cluster indexing number
	//return the cluster number of newly allocated cluster

	//fat table을 가져와야하고
	//특정 clst 정보만 있음 -> 이건 하나의 섹터인가? 
	//어디다 연결해야되지?				
	//clst에 지정된 clst뒤에 클러스터를 추가하연 체인을 확장
	//clst 는 index
	//지금 추가해야되는건 fat에 현재 인덱스의 value(클러스터의 다음 idx)

	//클러스터는 length 만큼의 배열 
	//fat_fs->fat[clst] = ;
	cluster_t new_clst = get_empty_cluster();
	//여기서 만들어지는게 클러스터의 시작인가, 단일 섹터인가?

	//fat_fs->fat[clst] = new_clst;
	if (new_clst != 0)
	{
		fat_put(new_clst, EOChain);
		if (clst != 0){
			fat_put(clst, new_clst);
		}
	}
	return new_clst;
}

void fat_remove_chain(cluster_t clst, cluster_t pclst){
	//체인에서 클러스터를 제거
	//pclst가 이전 클러스터 -> 제거하면 pclst가 마지막 클러스터가 되야한다.
	//clst를 free할 필요가 있나?
	while(clst && clst != EOChain){
		bitmap_set(fat_bitmap, clst - 1, false);
		clst = fat_get(clst);
	} 
	if (pclst != 0){
		fat_put(pclst, EOChain);
	}

}

void fat_put (cluster_t clst, cluster_t val){
	ASSERT(clst >= 1);
	if(!bitmap_test(fat_bitmap, clst - 1))
		bitmap_mark(fat_bitmap, clst - 1);
	fat_fs->fat[clst-1] = val;
	//또 다른 처리할 것 없나? 
	//cluster에 next cluster 세팅만 하면 되
}

cluster_t fat_get (cluster_t clst){
	return fat_fs->fat[clst-1];
}

disk_sector_t cluster_to_sector (cluster_t clst){
	ASSERT(clst >= 1);
	 return fat_fs->data_start + (clst - 1) * SECTORS_PER_CLUSTER;
	 //여기근데 51byte 넣어주어야 될것같은데
	 // 섹터는 512 만큼 널뛰기 
	 //sector size 가 아니라 그냥 섹터 
	 //data start이후에 잘리는게 512byte가 아니라 그냥 clst 인덱스인가? 
}