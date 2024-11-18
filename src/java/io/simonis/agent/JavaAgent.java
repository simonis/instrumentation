package io.simonis.agent;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.IllegalClassFormatException;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.Arrays;

public class JavaAgent {

  static String classPattern = "io/simonis/InstrumentationTest";

  // The class `InstrumentationTest` must contain a string constant of the form:
  // "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0"
  //  1234567890123456789012345678901234567890123456789
  //
  static byte[] stringPattern = new byte[] { (byte)'x', (byte)'x', (byte)'x', (byte)'x',
                                             (byte)'x', (byte)'x', (byte)'x', (byte)'x' };

  // 'stringPattern' must be included 6 times at the beginning of the string followed by the '0' character
  final static int patternCount = 6;

  // The watermark we write into the string if we transform the class
  final static byte[] watermark = {(byte)'J', (byte)'I', (byte)'N', (byte)'S', (byte)'T'};

  public static Instrumentation inst;

  public static void premain(String args, Instrumentation inst) {
    JavaAgent.inst = inst;
    byte id = (byte)'a';
    if (args != null && args.length() > 0) {
      id = (byte)args.charAt(0);
    }
    inst.addTransformer(new ClassTransformer(id), true);

    System.out.print("JINST - agent  ");
    System.out.print((char)id);
    System.out.print(" for  ");
    System.out.print(classPattern);
    System.out.println(" installed");
  }

  public static void agentmain(String args, Instrumentation inst) {
    premain(args, inst);
  }

  static class ClassTransformer implements ClassFileTransformer {
    private byte id;

    public ClassTransformer(byte id) {
      this.id = id;
    }

    // poor man's memmem()
    static int find(byte[] haystack, byte[] needle) {
      for (int i = 0; i <= haystack.length - needle.length; i++) {
        if (Arrays.equals(haystack, i, i + needle.length, needle, 0, needle.length)) {
          return i;
        }
      }
      return -1;
    }

    @Override
    public byte[] transform(ClassLoader loader,
                            String className,
                            Class<?> classBeingRedefined,
                            ProtectionDomain protectionDomain,
                            byte[] classfileBuffer) throws IllegalClassFormatException {

      if (!className.startsWith(classPattern)) return null;

      System.out.print("JINST - transform:    ");
      System.out.print(className);
      System.out.print(" ");
      System.out.println(classBeingRedefined == null ? "load" : "re-define/transform");

      int location = find(classfileBuffer, stringPattern);
      if (location >= 0) {
        byte version = classfileBuffer[location + (patternCount * stringPattern.length)];
        // Increment version for every transformation
        classfileBuffer[location + (patternCount * stringPattern.length)] = (byte)(version + 1);
        // Write the old version, watermark and id at the index given by the version (i.e. the oder of transformation)
        int newLocation = location + ((version - (byte)'0' + 1) * stringPattern.length);
        classfileBuffer[newLocation] = version;
        System.arraycopy(watermark, 0, classfileBuffer, newLocation + 1, watermark.length);
        classfileBuffer[newLocation + 1 + watermark.length] = id;
        return classfileBuffer;
      }

      return null;
    }
  }
}
