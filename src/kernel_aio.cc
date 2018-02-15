// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <pthread.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <libaio.h>
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "target.h"
#include "async_io.h"
#include "kernel_aio.h"

namespace diskspd {

	/**
	 *	Structure for representing an array of iocbs submitted to the kernel
	 */
	struct CbArray {
		size_t size;		// array size for bounds checking etc
		iocb** array;		// the actual array
		size_t items_left;	// a count of how many unfinished requests are in this array
	};

	class _KernelAsyncIop : public IAsyncIop {
		public:
			_KernelAsyncIop() {
				assert(!"Not implemented");
			}

			_KernelAsyncIop(
					Type t,
					int fd,
					off_t offset,
					void * read_buf,
					void * write_buf,
					size_t nbytes,
					int group_id,
					std::shared_ptr<TargetData> t_data,
					uint64_t time_stamp
					) {
				type = t;
				target_data = t_data;
				this->group_id = group_id;
				this->read_buf = read_buf;
				this->write_buf = write_buf;

				// set up iocb struct
				if (t == READ) {
					io_prep_pread(&cb, fd, read_buf, nbytes, offset);
				} else {
					io_prep_pwrite(&cb, fd, write_buf, nbytes, offset);
				}
			}

			~_KernelAsyncIop(){}

			Type get_type() { return type; }
			void set_type(Type t) {
				type = t;
				// update control block
				cb.aio_lio_opcode = t == READ ? IO_CMD_PREAD : IO_CMD_PWRITE;
				cb.u.c.buf = t == READ ? read_buf : write_buf;
			}

			int get_fd() { return cb.aio_fildes; }
			void set_fd(int fd) { cb.aio_fildes = fd; }

			off_t get_offset() { return cb.u.c.offset; }
			void set_offset(off_t o) { cb.u.c.offset = o; }

			size_t get_nbytes() { return cb.u.c.nbytes; }
			void set_nbytes(size_t n) { cb.u.c.nbytes = n; }

			int get_group_id() { return group_id; }
			void set_group_id(int id) { group_id = id; }

			uint64_t get_time() { return time; }
			void set_time(uint64_t time_stamp) { time = time_stamp; }

			std::shared_ptr<TargetData> get_target_data() { return target_data; }
			void set_target_data(std::shared_ptr<TargetData> t_data) { target_data = t_data; };

			int get_ret() { return result; }
			int get_errno() { return err; }

		private:
			friend _KernelAsyncIOManager;
			Type type = READ;
			int group_id = 0;
			// the control block where most stuff gets stored, used by the posix calls
			iocb cb;
			// these will be populated from the ioevent struct when an op completes
			int err = 0;
			int result = 0;
			// which cb array this was queued in
			std::shared_ptr<CbArray> cb_array;

			std::shared_ptr<TargetData> target_data;

			uint64_t time;

			void * read_buf;
			void * write_buf;

	};

	// this is the class that 'privately' implements the kernel aio classes
	class _KernelAsyncIOManager {

		public:

			_KernelAsyncIOManager() {}

			~_KernelAsyncIOManager() {
				for (auto e : groups) {
					assert(!io_destroy(e.second->ctx));
				}
			}

			bool start(int n_concurrent) {
				started = true;
				return true;
			}

			bool create_group(int group_id, int n_concurrent) {

				groups_mutex.lock();
				if(groups.count(group_id)) {
					d_printf("Group already exists\n");
					return false;
				}

				groups[group_id] = std::make_shared<Group>();

				int err = io_queue_init(n_concurrent, &(groups[group_id]->ctx));
				if (err) {
					errno = -err;
#ifdef ENABLE_DEBUG
					perror("io_setup failed");
#endif
					return false;
				}
				assert(groups[group_id]->ctx);

				groups_mutex.unlock();
				return true;
			}

			std::shared_ptr<IAsyncIop> construct(
					IAsyncIop::Type type,
					int fd,
					off_t offset,
					void* read_buf,
					void* write_buf,
					size_t nbytes,
					int group_id,
					std::shared_ptr<TargetData> t_data,
					uint64_t time_stamp
					) {
				if (!started) assert(!"IOManager not started!");

				// create the op
				auto op = std::make_shared<_KernelAsyncIop>(
						type,fd,offset,read_buf,write_buf,nbytes,group_id,t_data,time_stamp
					);

				// it gets upcast to IAsyncIop implicitly
				return op;
			}

			std::shared_ptr<IAsyncIop> construct(std::shared_ptr<IAsyncIop> a) {
				assert(!"Not implemented!");
				if (!started) assert(!"IOManager not started!");
				return a; // not correct
			}

			// NOTE assumes a given group is accessed only by a single thread
			int enqueue(std::shared_ptr<IAsyncIop> ia) {
				if (!started) assert(!"IOManager not started!");

				// cast down
				std::shared_ptr<_KernelAsyncIop> a = std::static_pointer_cast<_KernelAsyncIop>(ia);

				groups_mutex.lock();
				auto group = groups[a->group_id];
				groups_mutex.unlock();

				group->op_queue.push_back(a);

				return 0;
			}

			int submit(int group_id) {
				if (!started) assert(!"IOManager not started!");

				groups_mutex.lock();
				auto group = groups[group_id];
				groups_mutex.unlock();

				// create the array
				auto array = std::make_shared<CbArray>();
				array->size = group->op_queue.size();
				array->items_left = array->size;
				array->array = (iocb**)malloc(sizeof(iocb*)*array->size);

				int i = 0;
				for (auto& op : group->op_queue) {
					op->cb_array = array;	// copy pointer to the array struct so we can find it later

					// add the op to the in_flight map with a unique id
					while(group->in_flight.count(group->next_flight)) {
						group->next_flight = (void *)((size_t)(group->next_flight) + 1);
					}
					op->cb.data = group->next_flight;
					group->in_flight[group->next_flight] = op;
					group->next_flight = (void *)((size_t)(group->next_flight) + 1);

					// add the ops's cb to the array
					array->array[i++] = &(op->cb);
				}

				// do the actual io submission!
				int err = io_submit(group->ctx, array->size, array->array);
				// clean up and return an error
				if (err != array->size) {

					// clean up all the ops we tried to submit
					for (int j = 0; j < array->size; ++j) {
						io_cancel(group->ctx, array->array[j], nullptr);// cancel the op, ignore errors
						void * in_flight_id = array->array[j]->data;	// get the in flight id
						group->in_flight.erase(in_flight_id);			// remove from map
					}

					free(array->array); // free the malloc'd memory
					group->arrays.erase(array); // remove from the set of cbarrays

					d_printf("IOManager failed: io_submit returned %d instead of %lu\n", err, array->size);
					errno = -err; // libaio returns the error code negated so we convert it
					return err;
				}

				// clear the queued ops
				group->op_queue.clear();

				return 0;
			}

			std::shared_ptr<IAsyncIop> wait(int group_id) {
				if (!started) assert(!"IOManager not started!");

				groups_mutex.lock();
				auto group = groups[group_id];
				groups_mutex.unlock();

				// wait on a single event
				io_event event = {0};
				int err = io_getevents(group->ctx, 1, 1, &event, NULL);
				if (err != 1) {
					perror("IOManager failed: io_getevents returned unexpected result");
					exit(1);
				}

				// get the op
				void * in_flight_id = event.data;
				auto op = group->in_flight[in_flight_id];
				group->in_flight.erase(in_flight_id);

				// remove from cbarray, destroy array if need be
				auto array = op->cb_array;
				--array->items_left;
				if (!array->items_left) {
					free(array->array); // free the malloc'd memory
					group->arrays.erase(array); // remove from the set
					// the only remaining reference to this CbArray is in the ops that used it
					// it will be overwritten and eventually destroyed automatically
				}

				// update return and error fields
				op->result = (int)event.res;

				return op;
			}

		private:
			bool started = false;

			// we need to keep track of all the arrays we submit to the kernel for aio
			// we decrement items_left when an io in a given array completes, and deallocate it
			struct Group {
				io_context_t ctx = 0;

				// the currently enqueue()d but not submit()ted requests
				std::vector<std::shared_ptr<_KernelAsyncIop>> op_queue;

				// the iocb arrays
				std::set<std::shared_ptr<CbArray>> arrays;

				// map of in_flight ids (stored in iocb's aio_data while in flight) to the _KernelAsyncIop
				std::map<void *, std::shared_ptr<_KernelAsyncIop>> in_flight;
				void * next_flight = 0;
			};

			// map of group number to group
			std::map<int, std::shared_ptr<Group>> groups;
			std::mutex groups_mutex;

	};

	KernelAsyncIOManager::KernelAsyncIOManager() { p = new _KernelAsyncIOManager(); }
	KernelAsyncIOManager::~KernelAsyncIOManager() { delete p; }

	bool KernelAsyncIOManager::start(int n_concurrent) { return p->start(n_concurrent); }

	bool KernelAsyncIOManager::create_group(int group_id, int n_concurrent) {
		return p->create_group(group_id, n_concurrent);
	}

	std::shared_ptr<IAsyncIop> KernelAsyncIOManager::construct(
			IAsyncIop::Type type,
			int fd,
			off_t offset,
			void* read_buf,
			void* write_buf,
			size_t nbytes,
			int group_id,
			std::shared_ptr<TargetData> t_data,
			uint64_t time_stamp
			) {
		return p->construct(type, fd, offset, read_buf, write_buf, nbytes, group_id, t_data, time_stamp);
	}

	int KernelAsyncIOManager::enqueue(std::shared_ptr<IAsyncIop> a) {
		return p->enqueue(a);
	}

	int KernelAsyncIOManager::submit(int group_id) {
		return p->submit(group_id);
	}

	std::shared_ptr<IAsyncIop> KernelAsyncIOManager::wait(int group_id) {
		return p->wait(group_id);
	}

} // namespace diskspd
