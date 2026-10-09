import java.lang.classfile.*;
import java.lang.constant.*;
import java.nio.file.*;

// java EscSavePatch.java BLK.CLASS CLASSPATH - GuiScreen (blk, 1.12.2 obfuscated names): closing any screen with Escape
// saves the options first (Back and the touch close button both send Escape). Vanilla and OptiFine settings screens only
// save on their Done button, so slider changes (render distance, FPS, brightness) were lost when leaving with Escape.
// Inserted at the start of keyTyped, blk.a(CI)V:   if (keyCode == 1) this.j.t.b();   (mc.gameSettings.saveOptions())
public class EscSavePatch {
    public static void main(String[] a) throws Exception {
        Path p = Path.of(a[0]);
        ClassDesc blk = ClassDesc.of("blk"), bib = ClassDesc.of("bib"), bid = ClassDesc.of("bid");
        // stack maps get regenerated, which needs the class hierarchy: resolve against the game classpath
        java.net.URL[] cp = java.util.Arrays.stream(a[1].split(":")).filter(x -> !x.isEmpty())
            .map(x -> { try { return Path.of(x).toUri().toURL(); } catch (Exception ex) { throw new RuntimeException(ex); } })
            .toArray(java.net.URL[]::new);
        ClassFile cf = ClassFile.of(ClassFile.ClassHierarchyResolverOption.of(ClassHierarchyResolver.defaultResolver()
            .orElse(ClassHierarchyResolver.ofResourceParsing(new java.net.URLClassLoader(cp, null)))));
        int[] n = {0};
        byte[] out = cf.transformClass(cf.parse(p), ClassTransform.transformingMethodBodies(
            m -> m.methodName().equalsString("a") && m.methodType().equalsString("(CI)V"),
            CodeTransform.ofStateful(() -> new CodeTransform() {
                boolean done;
                public void accept(CodeBuilder cb, CodeElement e) {
                    if (!done) {
                        done = true; n[0]++;
                        Label skip = cb.newLabel();
                        cb.iload(2).iconst_1().if_icmpne(skip)
                          .aload(0).getfield(blk, "j", bib).getfield(bib, "t", bid)
                          .invokevirtual(bid, "b", MethodTypeDesc.of(ConstantDescs.CD_void))
                          .labelBinding(skip);
                    }
                    cb.with(e);
                }
            })));
        if (n[0] != 1) { System.err.println(p + ": keyTyped not found"); System.exit(1); }
        Files.write(p, out);
        System.out.println(p + ": Escape saves the options");
    }
}
