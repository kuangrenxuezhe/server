gcc -g3 -o server ./utils/*.* ./ae/anet.* ./ae/ae.* ./thread/thread.* ./test/*.* -I./ae/ -I./utils/ -Ithread -lpthread -DAE_TEST_MAIN  -DNN_USE_POLL -DNN_HAVE_SEMAPHORE -DHAVE_EPOLL
