## Initialization order of JVMTI and Java instrumentation agents

### Build

```console
$ export JAVA_HOME=...
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

In general, agents are initialized in the order they appear on the command line. But [Java Instrumentation Agents](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/package-summary.html) are implemented in pure Java and depend on [JVMTI](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html). They can therefore only be initialized once the JVM is in a state where it can execute Java code. This means that in reality, first all the native JVMTI agents get initialized very early in the JVM startup process (i.e. their [`Agent_OnLoad()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#onload) methods get called) in the order they appear on the command line. After that, once JVMTI posts the [`VMInit()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#VMInit) event, which means that the JVM is ready to execute Java code, all the Java Instrumentation Agents get initialized (i.e. the [`premain()` methods of their `Premain-Class`es](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/package-summary.html#starting-an-agent-from-the-command-line-interface-heading) get called) in the order the Java agents appear on the command line.

The `-agentpath`/`-javaagent` command line options are [both parsed](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/arguments.cpp#L2287-L2334) in [`Arguments::parse_each_vm_init_arg()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/arguments.cpp#L2142C6-L2142C39). At this point, both of them are treated uniformly and simply added to the [`JvmtiAgentList`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgentList.hpp#L35) which still maintains their relative order on the command line. Then, in [`Threads::create_vm()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/runtime/threads.cpp#L428), still very early in the init process and before any classes are loaded or Java threads are created, the JVM invokes [`JvmtiAgentList::load_agents()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgentList.cpp#L185) which iterates over all the agents in the `JvmtiAgentList` and transitively invokes their [`invoke_Agent_OnLoad()`](https://github.com/openjdk/jdk/blob/d52d13648612546ef4458579aff6daf965586a03/src/hotspot/share/prims/jvmtiAgent.cpp#L596) method.

This is where things get different for Java and native agents. For native agents, the JVM loads their corresponding shared library and executes its `Agent_OnLoad()` method. This is a method written by the user, where he can register callbacks which will be invoked for specific JVMTI events like [`VMInit()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#VMInit) or [`ClassFileLoadHook`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#ClassFileLoadHook) if he's interested in transforming the bytecode of classes before they are loaded. These are exactly the callbacks we've defined in our example JVMTI agent [`jvmtiAgent.cpp`](./src/jvmti/jvmtiAgent.cpp).

For Java agents, we can't simply call their `premain()` method, because the JVM can't execute bytecode at this early point in time. Instead, the JVM loads a predefined JVMTI agent (i.e. `lib/libinstrument.so`) which is part of every JDK and implemented in the `java.instrument` module by the `java.lang.instrument` and `sun.instrument` packages. The native JVMTI agent itself is implemented in [`src/java.instrument/share/native/libinstrument/JPLISAgent.c`](https://github.com/openjdk/jdk/blob/master/src/java.instrument/share/native/libinstrument/JPLISAgent.c). When loaded for a Java instrumentation agent, this JVMTI agent parses the manifest of the agent's jar file for the `Premain-Class` (in its `Agent_OnLoad` method [`DEF_Agent_OnLoad`](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/java.instrument/share/native/libinstrument/InvocationAdapter.c#L146C1-L146C17)) and [registers itself for the `VMInit` event](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/java.instrument/share/native/libinstrument/JPLISAgent.c#L309).

Much later, almost at the end of the JVM's initialization process in `Threads::create_vm()`, the JVM posts a [`JvmtiExport::post_vm_initialized()`](https://github.com/openjdk/jdk/blob/207832952be3e57faf3db9303d492faa391d507c/src/hotspot/share/runtime/threads.cpp#L827) event to all the registered JVMTI environments. And this is exactly the point where the implicit JVMTI agents which have been created for each Java instrumentation agent, get a chance to finally call the Java agent's `premain()` method which can now register [`ClassFileTransformer`s](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/ClassFileTransformer.html). Notice, that at this point in time, the JVM already loaded quite some classes. These classes can not be redefined from a Java instrumentation agent at load time any more. Instead, the Java agent would have to call [`Instrumentation::redefineClasses()`](https://docs.oracle.com/en/java/javase/22/docs/api/java.instrument/java/lang/instrument/Instrumentation.html#redefineClasses(java.lang.instrument.ClassDefinition...)) if he would like to instrument them as well.

### Explaining the Demo

The demo in this repository consists of three files. A trivial Java application [`InstrumentationTest.java`](./src/java/io/simonis/InstrumentationTest.java) which prints a constant string:
```java
public class InstrumentationTest {
  public static void main(String[] args) {
    System.out.println("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0");
    //                  1234567890123456789012345678901234567890123456789
  }
}
```
Then we have a Java instrumentation agent [`JavaAgent.java`](./src/java/io/simonis/agent/JavaAgent.java) and a native JVMTI agent [`jvmtiAgent.cpp`](./src/jvmti/jvmtiAgent.cpp) which both redefine the string constant in `InstrumentationTest` according to the following rules:

1. Look for the start of the `xxx..` sequence (stored as UTF-8 string in the constant pool of the class file).
2. Get the current version of the class file (represented by a character strting from '`0`') be reading the first character at the end of the `xxx..` sequence.
3. Increment the class file version at the end of the `xxx..` sequence.
4. Write the original version, a watermark ("`JVMTI`" for the native agent, "`JINST`" for the Java agent) and the agent ID (that's a character passed to the agents on the command line through the optional agent parameter - should be `a`, `b`, `c`, etc. to capture the agents order on the command line) into the original `xxx..` string at an offset computed from the updated class version value. E.g. for the first transformation, we would write `0JVMTIa` at offset `1 * offset` (with `offset == 8`) if the first instrumentation was done by a native agent which the ID `a`. For the second transformation from a java agent with ID `b`, we would write `1JINSTb` to index `2 * offset` in the `xxx..` string.

Running without an agent at all will simply print the original string:
```console
$ java -cp src/java io.simonis.InstrumentationTest
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0
```
If we add a first JVMTI agent, the output changes as follows:
```console
$ java -agentpath:./jvmtiAgent.so=a -cp src/java io.simonis.InstrumentationTest
JVMTI - agent  a for  io/simonis/InstrumentationTest installed
JVMTI - VMInit a
JVMTI - FileLoad:     io/simonis/InstrumentationTest (0x7ffff0158488)
JVMTI - ClassLoad:    io/simonis/InstrumentationTest
JVMTI - ClassPrepare: io/simonis/InstrumentationTest
xxxxxxxx0JVMTIaxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx1
```
We can see that the JVMTI agent `a` gets installed before the `VMInit` event is posted. Our native JVMTI agent registers callbacks for the [`VMInit()`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#VMInit), [`ClassFileLoadHook`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#ClassFileLoadHook), [`ClassLoad`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#ClassLoad), [`ClassPrepare`](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html#ClassPrepare) events. In the callback for the `ClassFileLoadHook`, the string constant in class `InstrumentationTest` is redefined as described above.

If we add a Java agent *before* the JVMTI agent on the command line, the output changes as follows:
```console
$ java -javaagent:JavaAgent.jar=a -agentpath:./jvmtiAgent.so=b -cp src/java io.simonis.InstrumentationTest
JVMTI - agent  b for  io/simonis/InstrumentationTest installed
JINST - agent  a for  io/simonis/InstrumentationTest installed
JVMTI - VMInit b
JVMTI - FileLoad:     io/simonis/InstrumentationTest (0x7ffff0157458)
JINST - transform:    io/simonis/InstrumentationTest load
JVMTI - ClassLoad:    io/simonis/InstrumentationTest
JVMTI - ClassPrepare: io/simonis/InstrumentationTest
xxxxxxxx0JVMTIbx1JINSTaxxxxxxxxxxxxxxxxxxxxxxxxx2
```
Although the Java agent `a` is now first on the command line, the native agent `b`'s `Agent_OnLoad()` method is called first (for the reasons outlined in the [Background](#background) section). But Java agent `a`'s implicitly assigned JVMTI agent still get's the `VMInit` event *before* JVMTI agent `b`, because `a` is before `b` on the command line. We just don't see this logged to the screen, because we haven't instrumented the implicit agent's `VMInit` callback. But we can infer it because we can see from the output that the Java agent's `premain()` method gets called before agent `b`'s `VMInit` callback (and we know that for Java agents the `premain()` call is triggered from the `VMInit` callback of its associated JVMTI agent). The output string confirms that JVMTI agent `b`'s transformation was applied first although it came after the Java agent `a` on the command line.

An example with two Java and two JVMTI agents on the command line can be found at the top of this file.

We can get more insights into the inner workings of JVMTI if we enable JVMTI tracing with `-XX:TraceJVMTI=.. -Xlog:jvmti=trace`:

```console
$ $JAVA_HOME/bin/java -XX:TraceJVMTI='VMInit+ts' -Xlog:jvmti=trace -javaagent:JavaAgent.jar=a -agentpath:./jvmtiAgent.so=b -cp src/java io.simonis.InstrumentationTest
[0.005s][trace][jvmti] Tracing the event: VMInit
JVMTI - agent  b for  io/simonis/InstrumentationTest installed
[0,082s][trace][jvmti] Trg VM init event triggered
[0,082s][trace][jvmti] Evt VM init event sent
JINST - agent  a for  io/simonis/InstrumentationTest installed
[0,086s][trace][jvmti] Evt VM init event sent
JVMTI - VMInit b
JVMTI - FileLoad:     io/simonis/InstrumentationTest (0x7ffff01574c8)
JINST - transform:    io/simonis/InstrumentationTest load
JVMTI - ClassLoad:    io/simonis/InstrumentationTest
JVMTI - ClassPrepare: io/simonis/InstrumentationTest
xxxxxxxx0JVMTIbx1JINSTaxxxxxxxxxxxxxxxxxxxxxxxxx2
```

This confirms our above reasoning, that the `VMInit` event gets triggered after the native JVMTI agent `b` has been installed and is then first sent to the Java agent `a`'s implicit JVMTI agent before it is also sent to our own JVMTI agent `b`.

Unfortunately, there's no real good documentation for the syntax of the `-XX:TraceJVMTI` option except a [source code comment in `jvmtiTrace.cpp`](https://github.com/openjdk/jdk/blob/c59adf68d9ac49b41fb778041e3949a8057e8d7f/src/hotspot/share/prims/jvmtiTrace.cpp#L40-L67) copied here for your convenience:

```
// Usage:
//    -XX:TraceJVMTI=DESC,DESC,DESC
//
//    DESC is   DOMAIN ACTION KIND
//
//    DOMAIN is function name
//              event name
//              "all" (all functions and events)
//              "func" (all functions except boring)
//              "allfunc" (all functions)
//              "event" (all events)
//              "ec" (event controller)
//
//    ACTION is "+" (add)
//              "-" (remove)
//
//    KIND is
//     for func
//              "i" (input params)
//              "e" (error returns)
//              "o" (output)
//     for event
//              "t" (event triggered aka posted)
//              "s" (event sent)
//
// Example:
//            -XX:TraceJVMTI=ec+,GetCallerFrame+ie,Breakpoint+s
```
If unsure what to use, start with `-XX:TraceJVMTI=all` but be aware that the output gets very verbose.