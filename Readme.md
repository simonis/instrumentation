## Initialization order of JVMTI and Java instrumentation agents

### Build

```console
$ $JAVA_HOME/bin/javac src/java/io/simonis/InstrumentationTest.java
$ $JAVA_HOME/bin/javac src/java/io/simonis/agent/JavaAgent.java
$ $JAVA_HOME/bin/jar -c -f JavaAgent.jar -m src/java/manifest.mf \
                     -C src/java/ io/simonis/agent/
$ g++ -fPIC -shared -I $JAVA_HOME/include/ -I $JAVA_HOME/include/linux/ \
      -o jvmtiAgent.so src/jvmti/jvmtiAgent.cpp
```

### Run

```console
$ $JAVA_HOME/bin/java -agentpath:./jvmtiAgent.so=a -javaagent:JavaAgent.jar=b \
                      -agentpath:./jvmtiAgent.so=c -javaagent:JavaAgent.jar=d \
                      -cp src/java io.simonis.InstrumentationTest
JVMTI - agent  a for  io/simonis/InstrumentationTest installed
JVMTI - agent  c for  io/simonis/InstrumentationTest installed
JVMTI - VMInit a
JINST - agent  b for  io/simonis/InstrumentationTest installed
JVMTI - VMInit c
JINST - agent  d for  io/simonis/InstrumentationTest installed
JVMTI - FileLoad:     io/simonis/InstrumentationTest (0x7ffff0157da8)
JVMTI - FileLoad:     io/simonis/InstrumentationTest (0x7ffff0157da8)
JINST - transform:    io/simonis/InstrumentationTest load
JINST - transform:    io/simonis/InstrumentationTest load
JVMTI - ClassLoad:    io/simonis/InstrumentationTest
JVMTI - ClassLoad:    io/simonis/InstrumentationTest
JVMTI - ClassPrepare: io/simonis/InstrumentationTest
JVMTI - ClassPrepare: io/simonis/InstrumentationTest
xxxxxxxx0JVMTIax1JVMTIcx2JINSTbx3JINSTdxxxxxxxxx4
```

### Background

