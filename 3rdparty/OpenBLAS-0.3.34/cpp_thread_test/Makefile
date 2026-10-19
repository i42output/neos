TOPDIR = ..
include $(TOPDIR)/Makefile.system

all :: dgemv_tester dgemm_tester dgemm_mixed_tester

dgemv_tester :
	$(CXX) $(COMMON_OPT) -Wall -Wextra -Wshadow -std=c++11 dgemv_thread_safety.cpp ../$(LIBNAME) $(EXTRALIB) $(FEXTRALIB) -o dgemv_tester
	./dgemv_tester

dgemm_tester : dgemv_tester
	$(CXX) $(COMMON_OPT) -Wall -Wextra -Wshadow -std=c++11 dgemm_thread_safety.cpp ../$(LIBNAME) $(EXTRALIB) $(FEXTRALIB) -o dgemm_tester
	./dgemm_tester

dgemm_mixed_tester : dgemm_tester
	$(CXX) $(COMMON_OPT) -Wall -Wextra -Wshadow -std=c++11 dgemm_thread_safety_mixed.cpp ../$(LIBNAME) $(EXTRALIB) $(FEXTRALIB) -o dgemm_mixed_tester
	./dgemm_mixed_tester

clean ::
	rm -f dgemv_tester dgemm_tester dgemm_mixed_tester
