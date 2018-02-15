// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cmath>
#include <assert.h>
#include <cstdint>

#include "IoBucketizer.h"

namespace diskspd {

	const uint64_t INVALID_BUCKET_DURATION = 0;

	IoBucketizer::IoBucketizer()
		: _bucketDuration(INVALID_BUCKET_DURATION),
		_validBuckets(0)
	{}

	void IoBucketizer::Initialize(uint64_t bucketDuration, size_t validBuckets)
	{
		assert(_bucketDuration == INVALID_BUCKET_DURATION);
		assert(bucketDuration != INVALID_BUCKET_DURATION);

		_bucketDuration = bucketDuration;
		_validBuckets = validBuckets;
		_vBuckets.reserve(_validBuckets);
	}

	void IoBucketizer::Add(uint64_t ioCompletionTime)
	{
		assert(_bucketDuration != INVALID_BUCKET_DURATION);

		size_t bucketNumber = static_cast<size_t>(ioCompletionTime / _bucketDuration);
		size_t currentSize = _vBuckets.size();
		if (currentSize < bucketNumber + 1)
		{
			_vBuckets.resize(bucketNumber + 1);
			// Zero the new entries. Note that size is 1-based and bucketNumber is 0-based.
			for (size_t i = currentSize; i <= bucketNumber; i++)
			{
				_vBuckets[i] = 0;
			}
		}
		_vBuckets[bucketNumber]++;
	}

	size_t IoBucketizer::GetNumberOfValidBuckets() const
	{
		// Buckets beyond this may exist since Add is willing to extend the vector
		// beyond the expected number of valid buckets, but they are not comparable
		// buckets (straggling IOs over the timespan boundary).
		return (_vBuckets.size() > _validBuckets ? _validBuckets : _vBuckets.size());
	}

	size_t IoBucketizer::GetNumberOfBuckets() const
	{
		return _vBuckets.size();
	}

	unsigned int IoBucketizer::GetIoBucket(size_t bucketNumber) const
	{
		return _vBuckets[bucketNumber];
	}

	double IoBucketizer::_GetMean() const
	{
		size_t numBuckets = GetNumberOfValidBuckets();
		double sum = 0;

		for (size_t i = 0; i < numBuckets; i++)
		{
			sum += static_cast<double>(_vBuckets[i]) / numBuckets;
		}

		return sum;
	}

	double IoBucketizer::GetStandardDeviation() const
	{
		size_t numBuckets = GetNumberOfValidBuckets();

		if(numBuckets == 0)
		{
			return 0.0;
		}

		double mean = _GetMean();
		double ssd = 0;

		for (size_t i = 0; i < numBuckets; i++)
		{
			double dev = static_cast<double>(_vBuckets[i]) - mean;
			double sqdev = dev*dev;
			ssd += sqdev;
		}

		return sqrt(ssd / numBuckets);
	}

	void IoBucketizer::Merge(const IoBucketizer& other)
	{
		if(other._vBuckets.size() > _vBuckets.size())
		{
			_vBuckets.resize(other._vBuckets.size());
		}
		if (other._validBuckets > _validBuckets)
		{
			_validBuckets = other._validBuckets;
		}
		for(size_t i = 0; i < other._vBuckets.size(); i++)
		{
			_vBuckets[i] += other.GetIoBucket(i);
		}
	}

} // namespace diskspd
