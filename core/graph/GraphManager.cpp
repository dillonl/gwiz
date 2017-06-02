#include "GraphManager.h"
#include "ReferenceGraph.h"
#include "core/alignment/AlignmentReporter.h"
#include "core/util/ThreadPool.hpp"
#include "core/variant/VariantList.h"
#include "core/mapping/MappingManager.h"
#include "core/mapping/GSSWMapping.h"

#include "core/alignment/BamAlignmentManager.h"
#include "core/alignment/BamAlignment.h"
#include "core/file/FastaFileWriter.h"

#include <queue>
#include <algorithm>
#include <functional>

//#include <string>

namespace graphite
{

	GraphManager::GraphManager(IReference::SharedPtr referencePtr, IVariantManager::SharedPtr variantManagerPtr, IAlignmentManager::SharedPtr alignmentManagerPtr, IAdjudicator::SharedPtr adjudicatorPtr) :
		m_reference_ptr(referencePtr),
		m_variant_manager_ptr(variantManagerPtr),
		m_alignment_manager_ptr(alignmentManagerPtr),
		m_adjudicator_ptr(adjudicatorPtr)
	{
	}

	void GraphManager::buildGraphs(Region::SharedPtr regionPtr, uint32_t readLength)
	{
		auto variantsListPtr = this->m_variant_manager_ptr->getVariantsInRegion(regionPtr);
		if (variantsListPtr->getCount() == 0) // if we don't have variants or alignments in the region, then return
		{
			return;
		}

		std::deque< std::shared_ptr< std::future< void > > > futureFunctions;

		// loop through variants and build and adjudicate graphs
		IVariant::SharedPtr variantPtr = nullptr;
		while (variantsListPtr->getNextVariant(variantPtr))
		{
			position startPosition = 0;
			position endPosition = 0;
			auto variantRegionPtrs = variantPtr->getRegions();
			if (variantRegionPtrs.size() > 0)
			{
				startPosition = variantPtr->getRegions()[0]->getStartPosition();
				endPosition = variantPtr->getRegions()[variantPtr->getRegions().size() - 1]->getEndPosition();
			}
			std::vector< IVariant::SharedPtr > variantPtrs;
			IVariant::SharedPtr nextVariantPtr = nullptr;
			variantPtrs.emplace_back(variantPtr);
			while (variantsListPtr->peekNextVariant(nextVariantPtr) && variantPtr->doesOverlap(nextVariantPtr) && (!variantPtr->isStructuralVariant() && !nextVariantPtr->isStructuralVariant()))
			{
				variantsListPtr->getNextVariant(nextVariantPtr);
				variantPtrs.emplace_back(nextVariantPtr);

				for (auto regionPtr : nextVariantPtr->getRegions())
				{
					auto tmpStartPosition = regionPtr->getStartPosition();
					auto tmpEndPosition = regionPtr->getEndPosition();
					startPosition = (tmpStartPosition < startPosition) ? tmpStartPosition : startPosition;
					endPosition = (tmpEndPosition > endPosition) ? tmpEndPosition : endPosition;
				}
			}
			// getting all the alignments in the variant's region
			std::vector< IAlignment::SharedPtr > alignmentPtrs;
			std::unordered_set< IAlignment* > alignmentPtrSet;
			for (auto regionVariantPtr : variantPtrs)
			{
				auto regionPtrs = regionVariantPtr->getRegions();
				for (auto regionPtr : regionPtrs)
				{
					auto regionAlignmentListPtr = this->m_alignment_manager_ptr->getAlignmentsInRegion(regionPtr);
					auto regionAlignmentPtrsCount = regionAlignmentListPtr->getCount();
					if (regionAlignmentPtrsCount > 0)
					{
						auto regionAlignmentPtrs = regionAlignmentListPtr->getAlignmentPtrs();
						// alignmentPtrs.insert(alignmentPtrs.end(), regionAlignmentPtrs.begin(), regionAlignmentPtrs.end()); // combine lists of alignments
						for (auto alignmentPtr : regionAlignmentPtrs)
						{
							if (alignmentPtrSet.find(alignmentPtr.get()) == alignmentPtrSet.end())
							{
								alignmentPtrSet.emplace(alignmentPtr.get());
								alignmentPtrs.emplace_back(alignmentPtr);
								if (alignmentPtr->getPosition() < startPosition) { startPosition = alignmentPtr->getPosition(); }
								if ((alignmentPtr->getPosition() + alignmentPtr->getLength()) > endPosition) { endPosition = alignmentPtr->getPosition() + alignmentPtr->getLength(); }
							}
						}
					}
				}
			}
			alignmentPtrSet.clear();
			if (alignmentPtrs.size() > 0)
			{
				// find the start and end position for the graph
				auto graphAlignmentRegion = std::make_shared< Region >(regionPtr->getReferenceID(), startPosition, endPosition, Region::BASED::ONE);
				for (auto i = 1; i < alignmentPtrs.size(); ++i)
				{
					if (alignmentPtrs[i]->getPosition() < startPosition) { startPosition = alignmentPtrs[i]->getPosition(); }
					if (alignmentPtrs[i]->getPosition() + alignmentPtrs[0]->getLength() > endPosition) { endPosition = alignmentPtrs[i]->getPosition() + alignmentPtrs[0]->getLength(); }
				}
				auto variantListPtr = std::make_shared< VariantList >(variantPtrs, this->m_reference_ptr);
				auto alignmentListPtr = std::make_shared< AlignmentList >(alignmentPtrs);
				constructAndAdjudicateGraph(variantListPtr, alignmentListPtr, graphAlignmentRegion, readLength);
			}
		}

		while (!futureFunctions.empty())
		{
			auto futureFunct = futureFunctions.front();
			futureFunctions.pop_front();
			if (futureFunct->wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
			{
				futureFunctions.emplace_back(futureFunct);
			}
		}
	}

	void GraphManager::constructAndAdjudicateGraph(IVariantList::SharedPtr variantsListPtr, IAlignmentList::SharedPtr alignmentListPtr, Region::SharedPtr regionPtr, uint32_t readLength)
	{
		uint32_t numGraphCopies = (alignmentListPtr->getCount() < ThreadPool::Instance()->getThreadCount()) ? alignmentListPtr->getCount() : ThreadPool::Instance()->getThreadCount();  // get the min of threadcount and alignment count, this is the num of simultanious threads processing this graph
		std::deque< std::shared_ptr< std::future< void > > > futureFunctions;

		auto gsswGraphPtr = std::make_shared< GSSWGraph >(this->m_reference_ptr, variantsListPtr, regionPtr, this->m_adjudicator_ptr->getMatchValue(), this->m_adjudicator_ptr->getMisMatchValue(), this->m_adjudicator_ptr->getGapOpenValue(), this->m_adjudicator_ptr->getGapExtensionValue(), numGraphCopies);
		gsswGraphPtr->constructGraph();

		auto referenceGraphPtr = std::make_shared< ReferenceGraph >(this->m_reference_ptr, variantsListPtr, regionPtr, this->m_adjudicator_ptr->getMatchValue(), this->m_adjudicator_ptr->getMisMatchValue(), this->m_adjudicator_ptr->getGapOpenValue(), this->m_adjudicator_ptr->getGapExtensionValue(), numGraphCopies);
		referenceGraphPtr->constructGraph();

        // Write fasta headers and sequences to file.
        {
            //std::vector< std::string > graphPathHeaders = gsswGraphPtr->getGraphPathHeaders();
            //std::lock_guard< std::mutex > lock(this->m_gssw_graph_mutex);
            std::vector< std::string > graphPathHeaders = gsswGraphPtr->getGraphPathHeaders();
            std::vector< std::string > graphPathSequences = gsswGraphPtr->getGraphPathSequences();

            // Store headers in member variable. Can also use a.insert(a.end(), b.begin(), b.end()); May be using std namespace.
            for (auto header : graphPathHeaders)
            {
                m_graph_path_headers.push_back(header.substr(1));
            }

            // Store sequence lengths in member variable.
            for (auto sequence : graphPathSequences)
            {
                m_graph_path_lengths.push_back(sequence.length());
            }

            std::lock_guard< std::mutex > lock(this->m_gssw_graph_mutex);
            graphite::FastaFileWriter fastaFileWriter;
            fastaFileWriter.open("TestFastaFile.fa");
            fastaFileWriter.write(graphPathHeaders, graphPathSequences);
            fastaFileWriter.close();
        }

		static int count = 0;

		IAlignment::SharedPtr alignmentPtr;
		auto alignmentPtrs = alignmentListPtr->getAlignmentPtrs();
        
        /*
        // Write sam header to file. Am trying to setup a getter function in BamAlignmentManager.h so that I can get the BamAlignmentReader from m_alignment_manager_ptr.
        graphite::BamAlignmentManager::SharedPtr bamAlignmentManagerPtr = std::dynamic_pointer_cast< graphite::BamAlignmentManager >(m_alignment_manager_ptr);
        std::ofstream samFile;
        samFile.open("NewSamFile.sam");

        samFile.close();     // Close the SAM file.
        */
        
		while (alignmentListPtr->getNextAlignment(alignmentPtr))
		{

			auto gsswGraphContainer = gsswGraphPtr->getGraphContainer();
			auto refGraphContainer = referenceGraphPtr->getGraphContainer();
			auto funct = [gsswGraphContainer, refGraphContainer, gsswGraphPtr, referenceGraphPtr, alignmentPtr, this, regionPtr]()
			{
				auto refTraceback = referenceGraphPtr->traceBackAlignment(alignmentPtr, refGraphContainer);
				auto referenceMappingPtr = std::make_shared< GSSWMapping >(refTraceback, alignmentPtr);
				auto referenceSWScore = referenceMappingPtr->getMappingScore();
				uint32_t referenceSWPercent = ((referenceSWScore / (double)(alignmentPtr->getLength() * this->m_adjudicator_ptr->getMatchValue())) * 100);

				auto tracebackPtr = gsswGraphPtr->traceBackAlignment(alignmentPtr, gsswGraphContainer);
				auto gsswMappingPtr = std::make_shared< GSSWMapping >(tracebackPtr, alignmentPtr);

                // Write Bam data to new sam file.
                // Verify new position calculation.
                // Need to modify column 3.
                // Want to doulbe check that the CIGAR string is correct.
                // Need to modify column 7.
                // Remember that SAM has to be sorted by position to be loaded into IGV.
                {
                    std::lock_guard< std::mutex > lock(this->m_gssw_graph_mutex);
                    graphite::BamAlignment::SharedPtr bamAlignmentPtr = std::dynamic_pointer_cast< graphite::BamAlignment >(alignmentPtr);
                    std::ofstream samFile;
                    samFile.open("SamAlignmentData.sam", std::ios::app);
                    for (auto headerName : m_graph_path_headers)
                    {

                    samFile
                        << bamAlignmentPtr->getName()                   << "\t" //  1. QNAME
                        << bamAlignmentPtr->getAlignmentFlag()          << "\t" //  2. FLAG
                        // May be able to get this amount from variant_manager to variantPtr to getChrom().
                        //<< regionPtr->getRegionString().substr(0, 5)    << "\t" //  3. RNAME Need to parse string to obtain the correct chromosome.
                        << headerName    << "\t" //  3. RNAME; need to modify so that it outputs the correct "reference" name.
                        << gsswMappingPtr->getPosition()                << "\t" //  4. POS New position.
                        << bamAlignmentPtr->getOriginalMapQuality()     << "\t" //  5. MAPQ
                        << gsswMappingPtr->getCigarString(m_adjudicator_ptr) << "\t" //  6. New CIGAR string.
                        << "*"                                          << "\t" // 7. Place holder for actual value.
                        //<< bamAlignmentPtr->getMateID()                 << "\t" //  7. RNEXT INCORRECT value.
                        << bamAlignmentPtr->getMatePosition() + 1       << "\t" //  8. PNEXT +1 because BamTools mate position is 0-based.
                        << bamAlignmentPtr->getTemplateLength()         << "\t" //  9. TLEN
                        << bamAlignmentPtr->getSequence()               << "\t" // 10. SEQ
                        << bamAlignmentPtr->getFastqQualities()         << "\t" // 11. QUAL
                        << std::endl; 
                    }

                    samFile.close();     // Close the SAM file.
                }

				auto gsswSWScore = referenceMappingPtr->getMappingScore();
				uint32_t gsswSWPercent = ((gsswSWScore / (double)(alignmentPtr->getLength() * this->m_adjudicator_ptr->getMatchValue())) * 100);

				if (this->m_adjudicator_ptr->adjudicateMapping(gsswMappingPtr, referenceSWPercent))
				{
					MappingManager::Instance()->registerMapping(gsswMappingPtr);
				}
		    };

			auto future = ThreadPool::Instance()->enqueue(funct);
			futureFunctions.push_back(future);
		}

		// wait for all the functions to complete (for all graphs to be created and adjudicated)
		while (!futureFunctions.empty())
		{
			auto futureFunct = futureFunctions.front();
			futureFunctions.pop_front();
			if (futureFunct->wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
			{
				futureFunctions.emplace_back(futureFunct);
			}
		}
	}

    std::vector< std::string > GraphManager::getGraphPathHeaders ()
    {
        return m_graph_path_headers;
    }
    
    std::vector< int > GraphManager::getGraphPathLengths ()
    {
        return m_graph_path_lengths;
    }
}
