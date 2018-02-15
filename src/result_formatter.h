// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <memory>

#ifndef DISKSPD_RESULT_FORMATTER_H
#define DISKSPD_RESULT_FORMATTER_H

namespace diskspd {

	class Profile;
	class Job;

	class IResultFormatter {
		public:
			IResultFormatter(){}
			virtual ~IResultFormatter(){}

			/**
			 *	Convert a Profile whose Jobs have completed into output
			 */
			virtual void output_results(const Profile& profile)=0;
	};

	class ResultFormatterText : public IResultFormatter {
		public:
			ResultFormatterText(){}
			~ResultFormatterText(){}
			void output_results(const Profile& profile) override;
		private:
			enum Type {
				READ,
				WRITE,
				RW,
			};
			void print_iops(const std::shared_ptr<Job>& job, Type t);
	};

	class ResultFormatterXML : public IResultFormatter {
		public:
			ResultFormatterXML(){
				assert(!"XML results not implemented!");
			}
			~ResultFormatterXML(){}
			void output_results(const Profile& profile);
	};

	class ResultFormatterJSON : public IResultFormatter {
		public:
			ResultFormatterJSON(){
				assert(!"JSON results not implemented!");
			}
			~ResultFormatterJSON(){}
			void output_results(const Profile& profile);
	};

}

#endif // DISKSPD_RESULT_FORMATTER_H
