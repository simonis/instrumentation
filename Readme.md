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

In general, agents are initialized in the order they appear on the command line. But [Java Instrumentation Agents](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/package-summary.html) are implemented in pure Java and depend on [JVMTI](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html). They can therefore only be initialized once the JVM is in a state where it can execute Java code. This means that in reality, first all the native JVMTI agents get initialized very early in the JVM startup process (i.e. their [`Agent_OnLoad()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#onload) methods get called) in the order they appear on the command line. After that, once JVMTI posts the [`VMInit()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#VMInit) event, which means that the JVM is ready to execute Java code, all the Java Instrumentation Agents get initialized (i.e. the [`premain()` methods of their `Premain-Class`es](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/package-summary.html#starting-an-agent-from-the-command-line-interface-heading) get called) in the order the agents appear on the command line.

The `-agentpath`/`-javaagent` command line options are [both parsed](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/arguments.cpp#L2287-L2334) in [`Arguments::parse_each_vm_init_arg()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/arguments.cpp#L2142C6-L2142C39). At this point, both of them are treated uniformly and simply added to the [`JvmtiAgentList`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgentList.hpp#L35) which still maintains their relative order on the command line. Then, in [`Threads::create_vm()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/threads.cpp#L428), still very early in the init process before any classes are loaded or Java threads are created, the JVM invokes [`JvmtiAgentList::load_agents()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgentList.cpp#L185) which iterates over all the agents in the `JvmtiAgentList` and transitively invokes their [`invoke_Agent_OnLoad()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgent.cpp#L596) method.

This is where things get different for Java and native agents. For native agents, the JVM loads the corresponding shared library and executes its `Agent_OnLoad()` method. This is a method written by the user, where he can register callbacks which will be invoked for specific JVMTI events like [`VMInit()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#VMInit) or [`ClassFileLoadHook`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#ClassFileLoadHook) if he's interested in transforming the bytecode of classes before they are loaded. These are exactly the callbacks we've defined in our example JVMTI agent [`jvmtiAgent.cpp`](./src/jvmti/jvmtiAgent.cpp).

For Java agents, we can't simply call their `premain()` method, because the JVM can't execute bytecode at this early point in time. Instead, the JVM loads a predefined JVMTI agent (i.e. `lib/libinstrument.so`) which is part of every JDK and implemented in the `java.instrument` module as part of the `java.lang.instrument` and `sun.instrument` packages. The native JVMTI agent itself is implemented in [`src/java.instrument/share/native/libinstrument/JPLISAgent.c`](https://github.com/openjdk/jdk/blob/master/src/java.instrument/share/native/libinstrument/JPLISAgent.c). When loaded for a Java instrumentation agent, this JVMTI agent parses the manifest of the agent's jar file for the `Premain-Class` (in its `Agent_OnLoad` method [`DEF_Agent_OnLoad`](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/java.instrument/share/native/libinstrument/InvocationAdapter.c#L146C1-L146C17)) class and [registers itself for the `VMInit` event](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/java.instrument/share/native/libinstrument/JPLISAgent.c#L309).

Much later, almost at the end of the JVM's initialization process in `Threads::create_vm()`, the JVM posts a [`JvmtiExport::post_vm_initialized()`](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/hotspot/share/runtime/threads.cpp#L827) event to all the registered JVMTI agents. And this is the point where the implicit JVMTI agents which have been created for each Java instrumentation agent, get a chance to finally call the Java agent's `premain()` method which can now register [`ClassFileTransformer`s](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/ClassFileTransformer.html). Notice, that at this point in time, the JVM already loaded quite some classes. From a Java instrumentation agent, these classes can not be redefined at load time any more. Instead, the Java agent would have to call [`Instrumentation::redefineClasses()`](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/Instrumentation.html#redefineClasses(java.lang.instrument.ClassDefinition...)) if he would like to instrument them as well.
