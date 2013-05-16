#include <signal.h>
#include <fstream>
#include <memory>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/date_time.hpp>
#include <boost/program_options.hpp>
#include "zmq.hpp"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;
using namespace zmq;
using namespace boost;

bool flag = false;
mutex flagMutex;
condition_variable flagCond;
std::shared_ptr<thread> sub_thread;
std::shared_ptr<thread> pub_thread;

void sig_handler(int) {
	if (sub_thread)
		sub_thread->interrupt();
}

void monitor_func(context_t * ctxt, const std::string addr, const std::string topic) {
	socket_t pair(*ctxt, ZMQ_PAIR);
	pair.connect(addr.c_str());
	pollitem_t items[1] = {{(void*)pair, 0, ZMQ_POLLIN, 0}};
	try {
		while(true) {
			this_thread::interruption_point();
			items[0].revents = 0;
			if(poll(items, sizeof(items)/sizeof(items[0]), 1000)>0) {
				message_t msg;
				pair.recv(&msg);
				const zmq_event_t * event = reinterpret_cast<const zmq_event_t*>(msg.data());
				ostringstream oss;
				oss << topic << ": ";
				switch (event->event) {
					case ZMQ_EVENT_CONNECTED:
						oss << "ZMQ_EVENT_CONNECTED (addr=" << event->data.connected.addr << ", fd="<<event->data.connected.fd<<")";
						break;
					case ZMQ_EVENT_CONNECT_DELAYED:
						oss << "ZMQ_EVENT_CONNECT_DELAYED (addr=" << event->data.connect_delayed.addr << ", err="<<strerror(event->data.connect_delayed.err)<<")";
						break;
					case ZMQ_EVENT_CONNECT_RETRIED:
						oss << "ZMQ_EVENT_CONNECT_RETRIED (addr=" << event->data.connect_retried.addr << ", interval="<<event->data.connect_retried.interval<<")";
						break;
					case ZMQ_EVENT_LISTENING:
						oss << "ZMQ_EVENT_LISTENING (addr=" << event->data.listening.addr << ", fd="<<event->data.listening.fd<<")";
						break;
					case ZMQ_EVENT_BIND_FAILED:
						oss << "ZMQ_EVENT_BIND_FAILED (addr=" << event->data.bind_failed.addr << ", err="<<strerror(event->data.bind_failed.err)<<")";
						break;
					case ZMQ_EVENT_ACCEPTED:
						oss << "ZMQ_EVENT_ACCEPTED (addr=" << event->data.accepted.addr << ", fd="<<event->data.accepted.fd<<")";
						break;
					case ZMQ_EVENT_ACCEPT_FAILED:
						oss << "ZMQ_EVENT_ACCEPT_FAILED (addr=" << event->data.accept_failed.addr << ", err="<<strerror(event->data.accept_failed.err)<<")";
						break;
					case ZMQ_EVENT_CLOSED:
						oss << "ZMQ_EVENT_CLOSED (addr=" << event->data.closed.addr << ", fd="<<event->data.closed.fd<<")";
						break;
					case ZMQ_EVENT_CLOSE_FAILED:
						oss << "ZMQ_EVENT_CLOSE_FAILED (addr=" << event->data.close_failed.addr << ", err="<<strerror(event->data.close_failed.err)<<")";
						break;
					case ZMQ_EVENT_DISCONNECTED:
						oss << "ZMQ_EVENT_DISCONNECTED (addr=" << event->data.disconnected.addr << ", fd="<<event->data.disconnected.fd<<")";
						break;
				}
				cerr << oss.str() << endl;
			}
		}
	} catch (thread_interrupted & e) {
	}
}

void pub_func(const program_options::variables_map vm) {
	context_t ctxt(1);
	socket_t pub(ctxt, ZMQ_PUB);
	zmq_socket_monitor(pub, "inproc://monitor.pub", ZMQ_EVENT_ALL);
	thread monitor_thread(monitor_func, &ctxt, "inproc://monitor.pub", "pub");
	std::string payload(vm["size"].as<size_t>(), '.');
	if (vm.count("no-linger")) {
		int linger = 0;
		pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	}
	if (vm.count("sndhwm")) {
		auto sndhwm = vm["sndhwm"].as<int>();
		pub.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
	}
	pub.bind("tcp://*:4404");
	{
		unique_lock<mutex> lock(flagMutex);
		while(flag == false) {
			flagCond.wait(lock);
		}
	}
	for(size_t pubCount=0; pubCount<vm["count"].as<size_t>(); ++pubCount) {
		for(size_t partNo=1; partNo<vm["parts"].as<size_t>(); ++partNo)
			assert(pub.send(payload.data(), payload.size(), ZMQ_SNDMORE)==payload.size());
		assert(pub.send(payload.data(), payload.size())==payload.size());
	}
	this_thread::sleep(posix_time::seconds(vm["recovery-time"].as<long>()));
	for(size_t pubCount=0; pubCount<vm["recovery-count"].as<size_t>(); ++pubCount) {
		if (vm["recovery-rate"].as<size_t>() > 0)
			this_thread::sleep(posix_time::millisec(1000.0/vm["recovery-rate"].as<size_t>()));
		pub.send("bye", 3);
	}
	monitor_thread.interrupt();
	monitor_thread.join();
}

void sub_func(const program_options::variables_map vm) {
	context_t ctxt(1);
	socket_t sub(ctxt, ZMQ_SUB);
	zmq_socket_monitor(sub, "inproc://monitor.sub", ZMQ_EVENT_ALL);
	thread monitor_thread(monitor_func, &ctxt, "inproc://monitor.sub", "sub");
	pollitem_t items[1] = {{(void*)sub, 0, ZMQ_POLLIN, 0}};
	sub.setsockopt(ZMQ_SUBSCRIBE, "", 0);
	if (vm.count("rcvhwm")) {
		auto rcvhwm = vm["rcvhwm"].as<int>();
		sub.setsockopt(ZMQ_SNDHWM, &rcvhwm, sizeof(rcvhwm));
	}
	sub.connect("tcp://localhost:4404");
	size_t subCount = 0;
	bool hi=true;
	try {
		while(true) {
			items[0].revents = 0;
			if(poll(items, sizeof(items)/sizeof(items[0]), 1000)>0) {
				message_t msg;
				sub.recv(&msg);
				if (hi && msg.size()==3){
					cout << subCount << endl;
					subCount=0;
					hi=false;
				}
				++subCount;
			} else {
				this_thread::interruption_point();
			}
			if(flag == false)
			{
				unique_lock<mutex> lock(flagMutex);
				flag = true;
				flagCond.notify_all();
			}
		}
	} catch (thread_interrupted & e) {
		cout << subCount << endl;
	}
	monitor_thread.interrupt();
	monitor_thread.join();
}

int main(int argc, const char**argv) {
	ostringstream helptext;
	helptext <<
		"Usage: " << argv[0] << " [OPTION] ...\n"\
		"Test ZMQ pub-sub failure.\n"\
		"\n"\
		"This tool sends some messages at a high rate, then waits a while, then\n"\
		"sends some more at the low rate. The idea is that even if the high rate messages\n"\
		"cause a failure, ZMQ should recover and allow the later messages to flow.\n"\
		"\n"\
		"Order of operations:\n"\
		"\n"\
		" 1. Start listening thread\n"\
		" 2. Start sending thread, setting ZMQ_RCVHWM if wanted.\n"\
		" 3. Start listening thread, setting ZMQ_SNDHWM if wanted.\n"\
		" 4. Sending thread seands *count* groups of *parts* messages, each\n"\
		"    *size* bytes long.\n"\
		" 5. Sending thread sleeps *recovery-time* seconds.\n"\
		" 6. Sending thread sends *recovery-count* messages.\n"\
		" 7. Listening thread prints the number of messages recieved from\n"\
		"    the first or second\n"\
		" 8. At this point or *recv-time* seconds after (4), whichever is\n"\
		"    later, terminate the listening thread if it does not recieve a\n"\
		"    message in a 100ms window.\n"\
		"\n"\
		"Options";
	program_options::options_description desc(helptext.str());
	desc.add_options()
		("help,h", "Print this help message.")
		("version", "Print program version.")
		("sndhwm", program_options::value<int>(), "set ZMQ_SNDHWM")
		("rcvhwm", program_options::value<int>(), "set ZMQ_RCVHWM")
		("no-linger", "Turn off ZMQ_LINGER.")
		("size", program_options::value<size_t>()->default_value(2), "payload size")
		("count", program_options::value<size_t>()->default_value(10000), "number of messages to send.")
		("parts", program_options::value<size_t>()->default_value(1), "number of message parts in each send.")
		("send", "Only start sending thread.")
		("recv", "Only start recieving thread.")
		("recovery-time", program_options::value<long>()->default_value(1), "Time to allow for recovery.")
		("recovery-count", program_options::value<size_t>()->default_value(100), "Number of messages to send as recovery.")
		("recv-time", program_options::value<long>()->default_value(0), "Time to allow recv to catch up.")
		("recovery-rate", program_options::value<size_t>()->default_value(0), "Rate at which recovery messages are sent (after recv-time) in messages per second. 0 means all at once after recv-time.");
	program_options::variables_map vm;
	try {
		program_options::store(program_options::parse_command_line(argc, argv, desc), vm);
	} catch (program_options::unknown_option & e) {
		cout << e.what() << endl << desc;
		return 1;
	} catch (program_options::invalid_option_value & e) {
		cout << e.what() << endl << desc;
		return 1;
	}
	if (vm.count("version")) {
		cout << PACKAGE << " " << VERSION << endl;
		return 0;
	}
	if (vm.count("help")) {
		cout << desc;
		return 0;
	}
	program_options::notify(vm);

	if (vm.count("recv") || !vm.count("send"))
		sub_thread.reset(new thread(sub_func, vm));
	if (vm.count("send") || !vm.count("recv")) {
		pub_thread.reset(new thread(pub_func, vm));
		if (vm.count("send") && !vm.count("recv")) {
			this_thread::sleep(posix_time::seconds(1));
			unique_lock<mutex> lock(flagMutex);
			flag = true;
			flagCond.notify_all();
		}
	} else {
		struct sigaction sa;
		sa.sa_handler = sig_handler;
		sigaction(SIGINT, &sa, NULL);
	}

	this_thread::sleep(posix_time::seconds(vm["recv-time"].as<long>()));
	if (pub_thread)
		pub_thread->join();
	if (pub_thread && sub_thread)
		sub_thread->interrupt();
	if (sub_thread)
		sub_thread->join();
}
