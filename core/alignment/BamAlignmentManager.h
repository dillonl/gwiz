#ifndef GRAPHITE_BAMALIGNMENTMANAGER_H
#define GRAPHITE_BAMALIGNMENTMANAGER_H

#include "IAlignmentManager.h"
#include "AlignmentReaderManager.hpp"
#include "BamAlignmentReader.h"
#include "core/region/Region.h"
#include "core/variant/IVariantList.h"
#include "core/variant/IVariantManager.h"
#include "core/sample/SampleManager.h"

#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <future>

namespace graphite
{
	class BamAlignmentManager : public IAlignmentManager
	{
	public:
		typedef std::shared_ptr< BamAlignmentManager > SharedPtr;
		BamAlignmentManager(SampleManager::SharedPtr sampleManager, Region::SharedPtr regionPtr, bool includeDuplicateReads = false);
		BamAlignmentManager(SampleManager::SharedPtr sampleManager, Region::SharedPtr regionPtr, AlignmentReaderManager< BamAlignmentReader >::SharedPtr alignmentReaderManagerPtr, bool includeDuplicateReads = false);
		~BamAlignmentManager();

		void loadAlignments(IVariantManager::SharedPtr variantManagerPtr);
		void waitForAlignmentsToLoad();
		void releaseResources() override;
		/* void processMappingStatistics() override; */
		SampleManager::SharedPtr getSamplePtrs() override;
		static std::vector< Sample::SharedPtr > GetSamplePtrs(std::vector< std::string >& bamPaths);
		static uint32_t GetReadLength(std::vector< std::string >& bamPaths);
	private:
		void loadAlignmentsHelper(const std::string& bamPath, std::vector< BamAlignmentReader::SharedPtr >& bamAlignmentReaders, std::deque< std::shared_ptr< std::future< std::vector< IAlignment::SharedPtr > > > >& futureFunctions, bool lastPositionSet, position bamLastPosition, Region::SharedPtr regionPtr, bool onlyUnmappedReads);

		std::mutex m_loaded_mutex;
		bool m_loaded;
        bool m_include_duplicate_reads;
		std::string m_bam_path;
		std::vector< std::shared_ptr< std::thread > > m_loading_thread_ptrs;
		std::mutex m_alignment_ptrs_lock;
		SampleManager::SharedPtr m_sample_manager_ptr;
		AlignmentReaderManager< BamAlignmentReader >::SharedPtr m_alignment_reader_manager;
	};
}

#endif //GRAPHITE_BAMALIGNMENTMANAGER_H
