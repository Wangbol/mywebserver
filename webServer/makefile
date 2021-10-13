TARGET=webserver
LIBS=-lpthread
OBJS=server.o taskQueue.o threadPool.o
$(TARGET):$(OBJS)
	$(CXX) $^ -o $@ $(LIBS) 
clean:
	$(RM) $(OBJS) $(TARGET)

.PHONY:clean
