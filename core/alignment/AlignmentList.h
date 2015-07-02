#ifndef GWIZ_ALIGNMENTLIST_H
#define GWIZ_ALIGNMENTLIST_H

#include "IAlignment.h"
#include "IAlignmentList.h"

namespace gwiz
{
	class AlignmentList : public IAlignmentList
	{
	public:
		typedef std::shared_ptr< AlignmentList > SharedPtr;
		typedef std::vector< IAlignment::SharedPtr >::iterator AlignmentIter;
		AlignmentList(std::vector< IAlignment::SharedPtr > alignmentPtrs);
		~AlignmentList();

		size_t getCount() override;
		void sort() override;
		bool getNextAlignment(IAlignment::SharedPtr& alignmentPtr) override;
	private:
		std::vector< IAlignment::SharedPtr > m_alignment_ptrs;
		size_t m_alignment_idx;
	};
}

#endif //GWIZ_ALIGNMENTLIST_H