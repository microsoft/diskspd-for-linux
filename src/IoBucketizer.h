// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <vector>
#include <cstdio>
#include <cstdint>

#ifndef DISKSPD_IO_BUCKETIZER_H
#define DISKSPD_IO_BUCKETIZER_H

namespace diskspd {
	class IoBucketizer
	{
	public:
		IoBucketizer();
		void Initialize(uint64_t bucketDuration, size_t validBuckets);

		size_t GetNumberOfValidBuckets() const;
		size_t GetNumberOfBuckets() const;
		unsigned int GetIoBucket(size_t bucketNumber) const;
		void Add(uint64_t ioCompletionTime);
		double GetStandardDeviation() const;
		void Merge(const IoBucketizer& other);
	private:
		double _GetMean() const;

		uint64_t _bucketDuration;
		size_t _validBuckets;
		std::vector<unsigned int> _vBuckets;
	};
}	// namespace diskspd

#endif // DISKSPD_IO_BUCKETIZER_H
