/*
 * linux/mm/page_isolation.c
 */

#include <linux/mm.h>
#include <linux/page-isolation.h>
#include <linux/pageblock-flags.h>
#include "internal.h"

static inline struct page *
__first_valid_page(unsigned long pfn, unsigned long nr_pages)
{
	int i;
	for (i = 0; i < nr_pages; i++)
		if (pfn_valid_within(pfn + i))
			break;
	if (unlikely(i == nr_pages))
	{
		cma_log(CMA_LOG_EMERG, "%s: error: return NULL\n", __FUNCTION__);	
		return NULL;
	}
	return pfn_to_page(pfn + i);
}

/*
 * start_isolate_page_range() -- make page-allocation-type of range of pages
 * to be MIGRATE_ISOLATE.
 * @start_pfn: The lower PFN of the range to be isolated.
 * @end_pfn: The upper PFN of the range to be isolated.
 *
 * Making page-allocation-type to be MIGRATE_ISOLATE means free pages in
 * the range will never be allocated. Any free pages and pages freed in the
 * future will not be allocated again.
 *
 * start_pfn/end_pfn must be aligned to pageblock_order.
 * Returns 0 on success and -EBUSY if any part of range cannot be isolated.
 */
int
start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	unsigned long undo_pfn;
	struct page *page;

	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));

	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && set_migratetype_isolate(page)) {
			undo_pfn = pfn;
			goto undo;
		}
	}
	return 0;
undo:
	for (pfn = start_pfn;
	     pfn < undo_pfn;
	     pfn += pageblock_nr_pages)
		unset_migratetype_isolate(pfn_to_page(pfn));

	return -EBUSY;
}

/*
 * Make isolated pages available again.
 */
int
undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct page *page;
	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));
	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (!page || get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
			continue;
		unset_migratetype_isolate(page);
	}
	return 0;
}
/*
 * Test all pages in the range is free(means isolated) or not.
 * all pages in [start_pfn...end_pfn) must be in the same zone.
 * zone->lock must be held before call this.
 *
 * Returns 1 if all pages in the range is isolated.
 */
static int
__test_page_isolated_in_pageblock(unsigned long pfn, unsigned long end_pfn)
{
	struct page *page;

	cma_log(CMA_LOG_DEBUG, "%s\n", __FUNCTION__); //qijiwen need check if it is called
	while (pfn < end_pfn) {
		if (!pfn_valid_within(pfn)) {
			pfn++;
			continue;
		}
		page = pfn_to_page(pfn);
		if (PageBuddy(page))
			pfn += 1 << page_order(page);
		else if (page_count(page) == 0 &&
				page_private(page) == MIGRATE_ISOLATE)
			pfn += 1;
		else
			break;
	}
	if (pfn < end_pfn)
		return 0;
	return 1;
}

int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn, flags;
	struct page *page;
	struct zone *zone;
	int ret;

	/*
	 * Note: pageblock_nr_page != MAX_ORDER. Then, chunks of free page
	 * is not aligned to pageblock_nr_pages.
	 * Then we just check pagetype fist.
	 */
	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
			break;
	}
	page = __first_valid_page(start_pfn, end_pfn - start_pfn);
	if ((pfn < end_pfn) || !page)
		return -EBUSY;
	/* Check all pages are free or Marked as ISOLATED */
	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	ret = __test_page_isolated_in_pageblock(start_pfn, end_pfn);
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret ? 0 : -EBUSY;
}

#ifdef CONFIG_CMA //added by qijiwen
//qijiwen from 3.10
void unset_migratetype_isolate_cma(struct page *page, unsigned migratetype)
{
	struct zone *zone;
	unsigned long flags, nr_pages;

	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	if (get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
		goto out;
	nr_pages = move_freepages_block(zone, page, migratetype); //qijiwen how to synchronize with other module
	__mod_zone_freepage_state(zone, nr_pages, migratetype);
	set_pageblock_migratetype(page, migratetype);
out:
	spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * start_isolate_page_range() -- make page-allocation-type of range of pages
 * to be MIGRATE_ISOLATE.
 * @start_pfn: The lower PFN of the range to be isolated.
 * @end_pfn: The upper PFN of the range to be isolated.
 * @migratetype: migrate type to set in error recovery.
 *
 * Making page-allocation-type to be MIGRATE_ISOLATE means free pages in
 * the range will never be allocated. Any free pages and pages freed in the
 * future will not be allocated again.
 *
 * start_pfn/end_pfn must be aligned to pageblock_order.
 * Returns 0 on success and -EBUSY if any part of range cannot be isolated.
 */
//qijiwen from 3.10 but is same with 3.0.8, just trust it.
int start_isolate_page_range_cma(unsigned long start_pfn, unsigned long end_pfn,
			     unsigned migratetype, bool skip_hwpoisoned_pages)
{
	unsigned long pfn;
	unsigned long undo_pfn;
	struct page *page;

	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));

	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page &&
		    set_migratetype_isolate_cma(page, skip_hwpoisoned_pages)) {
			undo_pfn = pfn;
			cma_log(CMA_LOG_WARNING, "set_migratetype_isolate_cma fail! %s\n", __FUNCTION__);
			goto undo;
		}
	}
	return 0;
undo:
	for (pfn = start_pfn;
	     pfn < undo_pfn;
	     pfn += pageblock_nr_pages)
		unset_migratetype_isolate_cma(pfn_to_page(pfn), migratetype);

	return -EBUSY;
}


/*
 * Make isolated pages available again.
 */
int undo_isolate_page_range_cma(unsigned long start_pfn, unsigned long end_pfn,
			    unsigned migratetype)
{
	unsigned long pfn;
	struct page *page;
	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));
	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (!page || get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
			continue;
		unset_migratetype_isolate_cma(page, migratetype);
	}
	return 0;
}

/*
 * Test all pages in the range is free(means isolated) or not.
 * all pages in [start_pfn...end_pfn) must be in the same zone.
 * zone->lock must be held before call this.
 *
 * Returns 1 if all pages in the range are isolated.
 */
static int
__test_page_isolated_in_pageblock_cma(unsigned long pfn, unsigned long end_pfn,
				  bool skip_hwpoisoned_pages, unsigned long *pfailed_pfn)
{
	struct page *page;

	while (pfn < end_pfn) {
		if (!pfn_valid_within(pfn)) {
			pfn++;
			continue;
		}
		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			/*
			 * If race between isolatation and allocation happens,
			 * some free pages could be in MIGRATE_MOVABLE list
			 * although pageblock's migratation type of the page
			 * is MIGRATE_ISOLATE. Catch it and move the page into
			 * MIGRATE_ISOLATE list.
			 */
			if (get_freepage_migratetype(page) != MIGRATE_ISOLATE) {
				struct page *end_page;

				end_page = page + (1 << page_order(page)) - 1;
				cma_log(CMA_LOG_INFO, "%s1: page:0x%08lx, end_page:0x%08lx migrate:%d\n", __FUNCTION__, page_to_pfn(page),  page_to_pfn(end_page), get_pageblock_migratetype(page));
				move_freepages(page_zone(page), page, end_page,
						MIGRATE_ISOLATE);
			}
			pfn += 1 << page_order(page);
		}
		else if (page_count(page) == 0 &&
			get_freepage_migratetype(page) == MIGRATE_ISOLATE)
			pfn += 1;
		else if (skip_hwpoisoned_pages && PageHWPoison(page)) {
			/*
			 * The HWPoisoned page may be not in buddy
			 * system, and page_count() is not 0.
			 */
			pfn++;
			continue;
		}
		else
		{
			if (PageHWPoison(page)) {
			/*
			 * The HWPoisoned page may be not in buddy
			 * system, and page_count() is not 0.
			 */
				cma_log(CMA_LOG_WARNING, "%s: PageHWPoison page:0x%08lx\n", __FUNCTION__, page_to_pfn(page));
			}
			else{
				cma_log(CMA_LOG_WARNING, "%s: page:0x%08lx private:0x%08lx migrate:%d MIGRATE:%d\n", __FUNCTION__, page_to_pfn(page), page_private(page), get_pageblock_migratetype(page), MIGRATE_ISOLATE);
				dump_page(page);
			}

			*pfailed_pfn = pfn;
			break;
		}
	}
	if (pfn < end_pfn)
	{
		cma_log(CMA_LOG_WARNING, "%s: pfn:0x%08lx, end_pfn:0x%08lx end_pfn-pfn:0x%08lx\n", __FUNCTION__, pfn,  end_pfn, end_pfn - pfn);
		return 0;
	}
	return 1;
}

int test_pages_isolated_cma(unsigned long start_pfn, unsigned long end_pfn,
			bool skip_hwpoisoned_pages, unsigned long *pfailed_pfn)
{
	unsigned long pfn, flags;
	struct page *page;
	struct zone *zone;
	int ret;

	/*
	 * Note: pageblock_nr_page != MAX_ORDER. Then, chunks of free page
	 * is not aligned to pageblock_nr_pages.
	 * Then we just check pagetype fist.
	 */
	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
		{
			cma_log(CMA_LOG_WARNING, "%s: page:%p  migratetype%d\n", __FUNCTION__, page, get_pageblock_migratetype(page));
			break;
		}
	}
	page = __first_valid_page(start_pfn, end_pfn - start_pfn);
	if ((pfn < end_pfn) || !page)
	{
		cma_log(CMA_LOG_WARNING, "%s: pfn:0x%lx end_pfn:0x%lx page:%p\n", __FUNCTION__, pfn, end_pfn, page);
		return -EBUSY;
	}
	/* Check all pages are free or Marked as ISOLATED */
	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	ret = __test_page_isolated_in_pageblock_cma(start_pfn, end_pfn,
						skip_hwpoisoned_pages, pfailed_pfn);
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret ? 0 : -EBUSY;
}

struct page *alloc_migrate_target(struct page *page, unsigned long private,
				  int **resultp)
{
	gfp_t gfp_mask = GFP_USER | __GFP_MOVABLE;

	if (PageHighMem(page))
		gfp_mask |= __GFP_HIGHMEM;

	return alloc_page(gfp_mask);
}
#endif //added by qijiwen end

