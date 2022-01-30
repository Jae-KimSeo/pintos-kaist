/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/thread.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	struct uninit_page *uninit = &page->uninit;
	// vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* Set up the handler */
	page->operations = &file_ops;

	struct lazy_load_info *info = (struct lazy_load_info *)aux;
	struct file_page *file_page = &page->file;
	file_page->file = info->file;
	file_page->length = info->page_read_bytes;
	file_page->offset = info->offset;

	//file page 초기화 
	// file, length, offset 
	//uninit page를 file-backed 로 초기화 
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_load_segment(struct page *page, void *aux)
{
	struct lazy_load_info * container = (struct lazy_load_info *)aux;
	struct file *file = container->file;
	size_t page_read_bytes = container->page_read_bytes;
	size_t page_zero_bytes = container->page_zero_bytes;
	off_t offset = container->offset;

	//palloc 되는 page의 위치 (가상 메모리)
	//왜 page->frame->kva 형태로 가져오는거지? 
	//va와 kva의 차이
	ASSERT(page->frame != NULL);
	void *kva = page->frame->kva;
	file_seek(file, offset);
	//이거 왜 kva?
	//얘는 유저 프로세스와 관련 없고 전체 운영체제에 매핑되어야 할 것 같긴함 (근거 없음)

	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes)
	{
		free(container);
		return false;
	}
	memset(kva + page_read_bytes, 0, page_zero_bytes);
	free(container);

	file_seek(file, offset);
	return true; 

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct file *mfile = file_reopen(file);

	//page = spt_find_page(&thread_current()->spt, addr);
	void *ori_addr = addr;
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	//spt 는 현재 프로세스에 대한 페이지 테이블 
	while (read_bytes > 0 || zero_bytes > 0){
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_info *container = malloc(sizeof(struct lazy_load_info));
		container->file = mfile;
		container->page_read_bytes = page_read_bytes;
		container->page_zero_bytes = 0;
		container->offset = offset;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, container))
			return NULL;
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr	   += PGSIZE;
		offset	   += page_read_bytes;
			//여기서 NULL 대신 이전까지의 page를 destroy 해야하나?
	}
	return ori_addr;
}
		

	
	

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page* page;
	while(page = spt_find_page(&thread_current()->spt, addr)){
		//struct page* page = spt_find_page(&thread_current()->spt, addr);
	//현재 가상 주소가 들어있는 supplemental page table 에서 page 찾기 
		//if (page == NULL)
			//break;
		struct lazy_load_info *container = (struct lazy_load_info *)page->uninit.aux;
		// container 을 사용해도 되고, file type object 의 멤버 변수들을 가져와도 된다.
		if (pml4_is_dirty(thread_current()->pml4, page->va)){
		//pml4가 dirty 상태 -> 변경이력이 있는 상태 
		//PTE가 install된 뒤 변경된 상태 
		//pml4 인자로 뭘 넣어야하지? virutal page 주소도 찾아야함 
		//wirte at을 위해 container 의 정보들이 필요함 어떻게 가져오지?
		//file 은 page type -> 페이지가 file 형태일 경우 관련된 정보를 가져올 수 있다.		
		//dirty bit 체크
			file_write_at(container->file, addr, container->page_read_bytes, container->offset);
		//현재 page의 변경 사항을 disk file에 업데이트 
			pml4_set_dirty(thread_current()->pml4, page, 0);		
		}// pml4에 대한 변경사항이 있을 경우 디스크에 업데이트 해주고
		pml4_clear_page(thread_current()->pml4, page->va);
		//pml4 자체는 clear해준다. vpage는 virutal page이므로 page의 virtual address
		//그냥 page가 들어가면 구조체의 주소, vpage로 pml4e_walk를 하므로 address가 들어가야한다.
		
		addr += PGSIZE;
	}
}
