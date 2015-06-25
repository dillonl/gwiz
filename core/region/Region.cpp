#include <stdexcept>
#include "boost/xpressive/xpressive.hpp"

#include "Region.h"

namespace gwiz
{
	using namespace boost::xpressive;

	Region::Region(const std::string& regionString) :
		m_region_string(regionString),
		m_start_position(0),
		m_end_position(0)
	{
		boost::spirit::qi::parse(regionString.c_str(), regionString.c_str() + regionString.size(), m_region_parser, this->m_reference_id, this->m_start_position, this->m_end_position);
		if (this->m_start_position > this->m_end_position || regionString.size() == 0)
		{
			throw std::invalid_argument("Region format is invalid");
		}
		if (this->m_start_position == 0 && this->m_end_position == 0)
		{
			this->m_end_position = MAX_POSITION;
		}
	}

	Region::~Region()
	{
	}
}
