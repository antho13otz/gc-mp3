package fr.gcstation.telecommande;

// Globe des radios : rendu OpenGL ES 2.0 (puce graphique du telephone), net a tous les zooms.
//  - Terre (res/raw, fait par tools/carte/android_globe.py) : terres et cotes tirees d'un champ de distance
//    8192 x 4096 (Natural Earth 1:10m) -> bord des terres au pixel pres et cote neon d'un pixel avec son halo, sans
//    escaliers ni flou meme tres pres ; relief eclaire par le soleil (neige en haut), ocean plus clair pres des cotes,
//    reflet du soleil sur la mer, lumieres des villes la nuit (NASA Black Marble) en neon, quadrillage fin,
//    frontieres, jour / nuit selon l'heure reelle, reflet bleu sur le bord ;
//  - ciel : Voie lactee et etoiles ; halo d'atmosphere ;
//  - radios : points lumineux (couleur = genre, taille = popularite), colonnes de lumiere pour les plus ecoutees ;
//  - anneaux : la radio choisie, celle qui joue sur la GameCube ; arc anime entre les deux.
// Camera : latitude / longitude / distance, avec inertie et vols animes (calcules ici, a chaque image).

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;
import android.opengl.Matrix;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.io.DataInputStream;
import java.io.InputStream;
import java.nio.ShortBuffer;
import java.util.zip.InflaterInputStream;
import java.util.Calendar;
import java.util.List;
import java.util.Random;
import java.util.TimeZone;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

class GlobeRenderer implements GLSurfaceView.Renderer {
    // ---- camera (ecrite par l'interface, lue ici) ----
    volatile float lat = 30, lon = 5, dist = 3.2f;
    volatile float vLat, vLon;                               // inertie (degres / s)
    volatile boolean touching, autoTurn = true;
    private float flyT = -1, flyLat0, flyLon0, flyD0, flyLat1, flyLon1, flyD1;
    private long lastNs;
    private float time;
    volatile long lastTouchMs;

    // ---- radios ----
    volatile List<RadioDir.Station> stations;
    volatile boolean[] visible;                              // (filtre : null = toutes)
    volatile int version;                                    // (change : refaire les tampons)
    private int builtVersion = -1;
    volatile int selected = -1, playing = -1;

    // ---- pour le choix au doigt et l'etiquette (lus par l'interface) ----
    final float[] mvp = new float[16], mv = new float[16];
    volatile int width = 1, height = 1;
    volatile float selX = -1, selY = -1;                     // (ecran ; -1 : cachee)
    interface FrameListener { void onFrame(); }
    volatile FrameListener frameListener;

    private final Context ctx;
    private final float[] skyMv = new float[16], skyMvp = new float[16];
    private final float[] proj = new float[16], view = new float[16], model = new float[16], tmp = new float[16], vp = new float[16];
    private int earthProg, ptProg, lineProg, haloProg, skyProg;
    private int sdfTex, nightTex, reliefTex, skyTex;
    // textures preparees en fond (gardees : le contexte OpenGL peut etre refait)
    private volatile ByteBuffer sdfPix, nightPix, reliefPix, skyPix;
    private volatile int sdfW, sdfH, nightW, nightH, reliefW, reliefH, skyW, skyH;
    private boolean texDone;
    private int maxTex = 4096;
    private FloatBuffer borderV;
    private int borderN;
    private FloatBuffer sphereV;
    private ShortBuffer sphereI;
    private int sphereCount;
    private FloatBuffer starV;
    private int starN;
    private FloatBuffer ptV, pillarV;
    private int ptN, pillarN;
    private final FloatBuffer markV = buf(16), arcV = buf(65 * 7), haloV = buf(8);
    private static final float FOV = 34f;

    GlobeRenderer(Context c) {
        ctx = c;
        new Thread(this::loadPixels, "globe-textures").start();       // (en fond : le globe s'affiche tout de suite)
    }

    // ---------------- textures (fichiers de res/raw) ----------------
    private void loadPixels() {
        try {
            nightPix = lum(R.raw.terre_nuit, true); 
            reliefPix = lum(R.raw.terre_relief, false);
            skyPix = lum(R.raw.ciel, false) ;
            InputStream in = ctx.getResources().openRawResource(R.raw.terre_sdf);
            DataInputStream di = new DataInputStream(in);
            byte[] h = new byte[12];
            di.readFully(h);
            int w = (h[4] & 255) | (h[5] & 255) << 8 | (h[6] & 255) << 16 | (h[7] & 255) << 24;
            int hh = (h[8] & 255) | (h[9] & 255) << 8 | (h[10] & 255) << 16 | (h[11] & 255) << 24;
            ByteBuffer b = ByteBuffer.allocateDirect(w * hh);
            InflaterInputStream z = new InflaterInputStream(di, new java.util.zip.Inflater(), 1 << 16);
            byte[] chunk = new byte[1 << 16];
            int n;
            while (b.hasRemaining() && (n = z.read(chunk, 0, Math.min(chunk.length, b.remaining()))) > 0) b.put(chunk, 0, n);
            z.close();
            b.position(0);
            sdfW = w; sdfH = hh;
            sdfPix = b;
        } catch (Throwable e) { }
    }
    /** image (JPEG) -> une couche (luminance) */
    private ByteBuffer lum(int res, boolean night) {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inScaled = false;
        Bitmap bm = BitmapFactory.decodeResource(ctx.getResources(), res, o);
        if (bm == null) return null;
        int w = bm.getWidth(), h = bm.getHeight();
        ByteBuffer b = ByteBuffer.allocateDirect(w * h);
        int[] row = new int[w];
        for (int y = 0; y < h; y++) {
            bm.getPixels(row, 0, w, 0, y, w, 1);
            for (int x = 0; x < w; x++) b.put((byte) ((row[x] >> 8) & 255));
        }
        b.position(0);
        bm.recycle();
        if (res == R.raw.terre_nuit) { nightW = w; nightH = h; }
        else if (res == R.raw.terre_relief) { reliefW = w; reliefH = h; }
        else { skyW = w; skyH = h; }
        return b;
    }
    /** envoie les textures a la puce graphique (des qu'elles sont pretes ; fil OpenGL) */
    private void uploadTextures() {
        if (texDone || sdfPix == null || nightPix == null || reliefPix == null || skyPix == null) return;
        GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
        ByteBuffer sd = sdfPix;
        int w = sdfW, h = sdfH;
        while (w > maxTex) {                                    // (puce limitee : moitie de la taille, moyenne de 4 points)
            ByteBuffer half = ByteBuffer.allocateDirect((w / 2) * (h / 2));
            for (int y = 0; y < h / 2; y++) for (int x = 0; x < w / 2; x++) {
                int a = sd.get(2 * y * w + 2 * x) & 255, b = sd.get(2 * y * w + 2 * x + 1) & 255, c = sd.get((2 * y + 1) * w + 2 * x) & 255, d = sd.get((2 * y + 1) * w + 2 * x + 1) & 255;
                half.put((byte) ((a + b + c + d + 2) / 4));
            }
            half.position(0);
            sd = half; w /= 2; h /= 2;
        }
        tex(sdfTex, sd, w, h, true);
        tex(nightTex, nightPix, nightW, nightH, true);
        tex(reliefTex, reliefPix, reliefW, reliefH, true);
        tex(skyTex, skyPix, skyW, skyH, false);
        texDone = true;
    }
    private static void tex(int id, ByteBuffer b, int w, int h, boolean repeat) {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id);
        b.position(0);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE, w, h, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, b);
        GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_REPEAT);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
    }

    /** frontieres (res/raw/traits.bin) : segments, juste au-dessus du sol */
    private void buildBorders() {
        try (DataInputStream in = new DataInputStream(new java.io.BufferedInputStream(ctx.getResources().openRawResource(R.raw.traits)))) {
            byte[] m = new byte[4];
            in.readFully(m);
            int nl = Integer.reverseBytes(in.readInt());
            java.util.ArrayList<float[]> segs = new java.util.ArrayList<>();
            int total = 0;
            float[] p = new float[3];
            for (int k = 0; k < nl; k++) {
                int type = in.readUnsignedByte();
                int np = Integer.reverseBytes(in.readInt());
                float[] l = new float[np * 3];
                for (int i = 0; i < np; i++) {
                    float lo = Short.reverseBytes(in.readShort()) * 180f / 32767f, la = Short.reverseBytes(in.readShort()) * 180f / 32767f;
                    toXYZ(la, lo, 1.0015f, l, i * 3);
                }
                if (type == 2 && np >= 2) { segs.add(l); total += (np - 1) * 2; }
            }
            float[] v = new float[total * 7];
            int o = 0;
            for (float[] l : segs) for (int i = 0; i + 5 < l.length; i += 3) {
                for (int e = 0; e < 2; e++) {
                    v[o++] = l[i + e * 3]; v[o++] = l[i + e * 3 + 1]; v[o++] = l[i + e * 3 + 2];
                    v[o++] = 0.55f; v[o++] = 0.75f; v[o++] = 1.0f; v[o++] = 0.30f;
                }
            }
            borderV = buf(Math.max(7, v.length));
            borderV.put(v).position(0);
            borderN = total;
        } catch (Exception e) { borderN = 0; }
    }

    static FloatBuffer buf(int floats) { return ByteBuffer.allocateDirect(floats * 4).order(ByteOrder.nativeOrder()).asFloatBuffer(); }

    // ---------------- shaders ----------------
    private static final String EARTH_V =
        "attribute vec3 aPos; attribute vec2 aUv; uniform mat4 uMVP; uniform mat4 uMV;" +
        "varying vec2 vUv; varying vec3 vN; varying vec3 vNv;" +
        "void main(){ vUv=aUv; vN=aPos; vNv=normalize((uMV*vec4(aPos,0.0)).xyz); gl_Position=uMVP*vec4(aPos,1.0); }";
    private static final String EARTH_F =
        "#extension GL_OES_standard_derivatives : enable\n" +
        "precision highp float; uniform sampler2D uSdf; uniform sampler2D uNight; uniform sampler2D uRelief;" +
        "uniform vec3 uSun; uniform vec3 uCam; uniform float uReady;" +
        "varying vec2 vUv; varying vec3 vN; varying vec3 vNv;" +
        "void main(){" +
        " vec3 n=normalize(vN);" +
        " float sd=(texture2D(uSdf,vUv).r*255.0-128.0)/8.0;" +                 // distance au rivage (points de la carte)
        " float fw=max(fwidth(sd),1e-4); float dpx=sd/fw;" +                    // ... en pixels d'ecran
        " float land=clamp(0.5+dpx,0.0,1.0)*uReady;" +
        " float core=clamp(1.6-abs(dpx),0.0,1.0)*uReady;" +                    // trait de cote : ~1 pixel, net
        " float glow=exp(-abs(dpx)/6.0)*uReady;" +
        " vec2 rt=vec2(1.0/2048.0,1.0/1024.0);" +
        " float hC=texture2D(uRelief,vUv).r, hE=texture2D(uRelief,vUv+vec2(rt.x,0.0)).r, hN=texture2D(uRelief,vUv-vec2(0.0,rt.y)).r;" +
        " vec3 east=normalize(cross(vec3(0.0,1.0,0.0),n)+vec3(1e-5,0.0,0.0)); vec3 north=cross(n,east);" +
        " vec3 nb=normalize(n-(east*(hE-hC)+north*(hN-hC))*5.0*land);" +      // relief
        " float sg=dot(n,uSun), sl=dot(nb,uSun);" +
        " float day=smoothstep(-0.12,0.22,sg);" +
        " vec3 ocean=mix(vec3(0.022,0.03,0.15),vec3(0.05,0.15,0.34),exp(sd*0.15)*0.85*uReady);" +   // indigo profond, plus clair pres des cotes
        " vec3 landc=mix(vec3(0.20,0.07,0.33),vec3(0.36,0.16,0.52),smoothstep(0.0,0.6,hC));" +
        " landc=mix(landc,vec3(0.78,0.84,1.0),smoothstep(0.62,0.9,hC)*0.45+smoothstep(0.86,0.97,abs(n.y))*0.55);" +   // neige, glace
        " vec3 c=mix(ocean,landc,land);" +
        " float lam=mix(max(sg,0.0),max(sl,0.0),land);" +
        " c*=0.38+1.10*day*(0.25+0.75*lam);" +
        " c+=vec3(0.02,0.03,0.07)*(1.0-day)*(0.6+0.4*land);" +                  // nuit : clair de lune bleute
        " vec3 V=normalize(uCam-n); vec3 Rf=reflect(-uSun,n);" +
        " c+=vec3(0.65,0.88,1.0)*pow(max(dot(Rf,V),0.0),70.0)*(1.0-land)*day*0.85;" +              // reflet du soleil sur la mer
        " float li=texture2D(uNight,vUv).r; float night=1.0-smoothstep(-0.22,0.08,sg);" +
        " float mag=clamp((1.0-fwidth(vUv.x)*4096.0)*1.5,0.0,1.0);" +            // (de pres : l'image des lumieres est agrandie)
        " float lw=fwidth(li)*0.8+0.002; float crisp=smoothstep(0.22-lw,0.22+lw,li)*(0.45+0.55*li);" +   // ... bords francs, pas de flou
        " li=mix(pow(li,1.3),crisp*0.85+pow(li,1.3)*0.35,mag);" +
        " c+=mix(vec3(1.0,0.22,0.78),vec3(1.0,0.82,0.55),li)*li*(night*1.5+0.05)*land;" +   // lumieres des villes
        " float la=(0.5-vUv.y)*12.0, lo=vUv.x*24.0;" +                         // quadrillage tous les 15 degres (fin)
        " float gl=abs(fract(la+0.5)-0.5)/max(fwidth(la),1e-4), go=abs(fract(lo+0.5)-0.5)/max(fwidth(lo),1e-4);" +
        " float grid=max(1.0-smoothstep(0.4,1.2,gl),(1.0-smoothstep(0.4,1.2,go))*smoothstep(0.98,0.9,abs(n.y)));" +
        " c+=vec3(0.15,0.6,0.9)*grid*0.07*(1.0-land*0.6);" +
        " c+=vec3(1.0,0.18,0.80)*glow*0.30*(0.6+0.4*night)+vec3(1.0,0.80,0.97)*core*(0.50+0.30*night);" +   // cotes neon
        " c+=vec3(1.0,0.45,0.25)*(1.0-smoothstep(0.0,0.07,abs(sg)))*0.09;" +   // ligne du soir
        " float rim=pow(1.0-max(vNv.z,0.0),3.0); c+=mix(vec3(0.13,0.85,1.0),vec3(1.0,0.3,0.8),0.25)*rim*(0.25+0.45*day);" +
        " gl_FragColor=vec4(c,1.0); }";
    private static final String SKY_V =
        "attribute vec3 aPos; attribute vec2 aUv; uniform mat4 uMVP; varying vec2 vUv;" +
        "void main(){ vUv=aUv; gl_Position=uMVP*vec4(aPos*40.0,1.0); }";
    private static final String SKY_F =
        "precision mediump float; uniform sampler2D uSky; varying vec2 vUv;" +
        "void main(){ float v=texture2D(uSky,vUv).r; v=pow(v,1.6)*0.55; gl_FragColor=vec4(v*vec3(0.75,0.62,1.0),1.0); }";
    private static final String PT_V =
        "attribute vec3 aPos; attribute vec4 aCol; attribute float aSize; uniform mat4 uMVP; uniform mat4 uMV; uniform float uScale; uniform float uMode;" +
        "varying vec4 vCol;" +
        "void main(){ gl_Position=uMVP*vec4(aPos,1.0); vec3 p=(uMV*vec4(aPos,1.0)).xyz; vec3 n=normalize((uMV*vec4(aPos,0.0)).xyz);" +
        " float f=uMode>1.5?1.0:dot(n,normalize(-p)); vCol=aCol*smoothstep(0.02,0.2,f); gl_PointSize=aSize*uScale; }";
    private static final String PT_F =
        "#extension GL_OES_standard_derivatives : enable\n" +
        "precision mediump float; varying vec4 vCol; uniform float uMode; uniform float uTime;" +
        "void main(){ float d=length(gl_PointCoord-vec2(0.5))*2.0; if(d>1.0) discard;" +
        " float a; vec3 c=vCol.rgb;" +
        " if(uMode<0.5){ float aa=fwidth(d)*1.2; float core=1.0-smoothstep(0.36-aa,0.36+aa,d); a=max(core,pow(1.0-d,3.0)*0.45); c=mix(c,vec3(1.0),core*smoothstep(0.30,0.0,d)*0.8); }" +   // point : coeur net (1 pixel de bord), petit halo
        " else if(uMode<1.5){ float r=0.62+0.12*sin(uTime*4.0); a=smoothstep(0.10,0.0,abs(d-r))+0.25*smoothstep(0.35,0.0,abs(d-r)); }" +   // anneau
        " else { a=pow(1.0-d,3.0)*0.9; }" +                                       // etoile
        " gl_FragColor=vec4(c*a*vCol.a,a*vCol.a); }";
    private static final String LINE_V =
        "attribute vec3 aPos; attribute vec4 aCol; uniform mat4 uMVP; uniform mat4 uMV; varying vec4 vCol;" +
        "void main(){ gl_Position=uMVP*vec4(aPos,1.0); vec3 p=(uMV*vec4(aPos,1.0)).xyz; vec3 n=normalize((uMV*vec4(normalize(aPos),0.0)).xyz);" +
        " vCol=aCol*smoothstep(-0.05,0.15,dot(n,normalize(-p))); }";
    private static final String LINE_F =
        "precision mediump float; varying vec4 vCol; uniform float uTime; uniform float uDash;" +
        "void main(){ float a=vCol.a; if(uDash>0.5) a*=0.35+0.65*smoothstep(0.4,1.0,fract(vCol.a*8.0-uTime*1.2));" +
        " gl_FragColor=vec4(vCol.rgb*a,a); }";
    private static final String HALO_V =
        "attribute vec2 aPos; varying vec2 vP; uniform vec2 uR; void main(){ vP=aPos; gl_Position=vec4(aPos*uR,0.0,1.0); }";
    private static final String HALO_F =
        "precision mediump float; varying vec2 vP; uniform float uK;" +
        "void main(){ float d=length(vP)*uK; float a=d<1.0?pow(d,8.0)*0.35:exp(-(d-1.0)*9.0);" +
        " vec3 c=mix(vec3(0.13,0.91,1.0),vec3(1.0,0.18,0.82),clamp((vP.y+1.0)*0.5,0.0,1.0)*0.6);" +
        " gl_FragColor=vec4(c*a*0.9,a*0.9); }";

    private static int shader(int type, String src) {
        int s = GLES20.glCreateShader(type);
        GLES20.glShaderSource(s, src);
        GLES20.glCompileShader(s);
        return s;
    }
    private static int program(String v, String f) {
        int p = GLES20.glCreateProgram();
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, v));
        GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, f));
        GLES20.glLinkProgram(p);
        return p;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig cfg) {
        GLES20.glClearColor(0.016f, 0.004f, 0.04f, 1f);
        earthProg = program(EARTH_V, EARTH_F);
        ptProg = program(PT_V, PT_F);
        lineProg = program(LINE_V, LINE_F);
        haloProg = program(HALO_V, HALO_F);
        buildSphere(128, 256);
        buildStars();
        haloV.position(0);
        haloV.put(new float[] { -1, -1, 1, -1, -1, 1, 1, 1 });
        skyProg = program(SKY_V, SKY_F);
        int[] mx = new int[1];
        GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, mx, 0);
        maxTex = mx[0] > 0 ? mx[0] : 4096;
        int[] t = new int[4];
        GLES20.glGenTextures(4, t, 0);
        sdfTex = t[0]; nightTex = t[1]; reliefTex = t[2]; skyTex = t[3];
        texDone = false;
        buildBorders();
        builtVersion = -1;
        lastNs = 0;
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int w, int h) {
        GLES20.glViewport(0, 0, w, h);
        width = w; height = h;
        Matrix.perspectiveM(proj, 0, FOV, (float) w / Math.max(1, h), 0.05f, 100f);
    }

    // ---------------- geometrie ----------------
    static void toXYZ(float lat, float lon, float r, float[] o, int k) {
        double la = Math.toRadians(lat), lo = Math.toRadians(lon);
        o[k] = (float) (Math.cos(la) * Math.sin(lo)) * r;
        o[k + 1] = (float) Math.sin(la) * r;
        o[k + 2] = (float) (Math.cos(la) * Math.cos(lo)) * r;
    }

    private final float[] sunV = new float[3];
    private void drawSphere(int prog) {
        int ap = GLES20.glGetAttribLocation(prog, "aPos"), au = GLES20.glGetAttribLocation(prog, "aUv");
        sphereV.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 20, sphereV);
        sphereV.position(3);
        GLES20.glVertexAttribPointer(au, 2, GLES20.GL_FLOAT, false, 20, sphereV);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(au);
        sphereI.position(0);
        GLES20.glDrawElements(GLES20.GL_TRIANGLES, sphereCount, GLES20.GL_UNSIGNED_SHORT, sphereI);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(au);
        sphereV.position(0);
    }

    private void buildSphere(int nLat, int nLon) {
        float[] v = new float[(nLat + 1) * (nLon + 1) * 5];
        int k = 0;
        for (int i = 0; i <= nLat; i++) {
            float la = 90f - 180f * i / nLat;
            for (int j = 0; j <= nLon; j++) {
                float lo = -180f + 360f * j / nLon;
                toXYZ(la, lo, 1f, v, k);
                v[k + 3] = (float) j / nLon;
                v[k + 4] = (float) i / nLat;
                k += 5;
            }
        }
        short[] idx = new short[nLat * nLon * 6];
        int n = 0;
        for (int i = 0; i < nLat; i++) for (int j = 0; j < nLon; j++) {
            int a = i * (nLon + 1) + j, b = a + nLon + 1;
            idx[n++] = (short) a; idx[n++] = (short) b; idx[n++] = (short) (a + 1);
            idx[n++] = (short) (a + 1); idx[n++] = (short) b; idx[n++] = (short) (b + 1);
        }
        sphereV = buf(v.length); sphereV.put(v).position(0);
        sphereI = ByteBuffer.allocateDirect(idx.length * 2).order(ByteOrder.nativeOrder()).asShortBuffer();
        sphereI.put(idx).position(0);
        sphereCount = idx.length;
    }

    private void buildStars() {
        Random r = new Random(11);
        starN = 900;
        float[] v = new float[starN * 8];
        for (int i = 0; i < starN; i++) {
            float la = (float) Math.toDegrees(Math.asin(r.nextFloat() * 2 - 1)), lo = r.nextFloat() * 360 - 180;
            toXYZ(la, lo, 40f, v, i * 8);
            float b = 0.35f + r.nextFloat() * 0.65f, tint = r.nextFloat();
            v[i * 8 + 3] = b * (tint < 0.2f ? 1f : 0.85f); v[i * 8 + 4] = b * 0.85f; v[i * 8 + 5] = b * (tint > 0.7f ? 1f : 0.9f); v[i * 8 + 6] = b;
            v[i * 8 + 7] = 1.5f + r.nextFloat() * r.nextFloat() * 4.5f;
        }
        starV = buf(v.length); starV.put(v).position(0);
    }

    /** points et colonnes de lumiere des radios (refaits quand la liste ou le filtre change) */
    private void buildStations() {
        List<RadioDir.Station> st = stations;
        boolean[] vis = visible;
        builtVersion = version;
        if (st == null) { ptN = pillarN = 0; return; }
        int n = st.size();
        float[] v = new float[n * 8];
        int maxClicks = 1;
        for (RadioDir.Station s : st) if (s.clicks > maxClicks) maxClicks = s.clicks;
        double lm = Math.log(1 + maxClicks);
        int k = 0, np = 0;
        float[] pl = new float[Math.min(n, 400) * 2 * 7];
        int pk = 0;
        float[] xyz = new float[3];
        for (int i = 0; i < n; i++) {
            if (vis != null && (i >= vis.length || !vis[i])) continue;
            RadioDir.Station s = st.get(i);
            toXYZ(s.lat, s.lon, 1.004f, v, k);
            int c = RadioDir.GENRE_COL[s.genre];
            float pop = (float) (Math.log(1 + s.clicks) / lm);
            v[k + 3] = ((c >> 16) & 255) / 255f; v[k + 4] = ((c >> 8) & 255) / 255f; v[k + 5] = (c & 255) / 255f;
            v[k + 6] = 0.55f + 0.45f * pop;
            v[k + 7] = 5f + 13f * pop;
            k += 8;
            if (np < 400 && pop > 0.62f) {                      // colonnes : les plus ecoutees
                float h = 1.004f + 0.02f + 0.16f * (pop - 0.62f) / 0.38f;
                toXYZ(s.lat, s.lon, 1.004f, xyz, 0);
                pl[pk++] = xyz[0]; pl[pk++] = xyz[1]; pl[pk++] = xyz[2];
                pl[pk++] = v[k - 5]; pl[pk++] = v[k - 4]; pl[pk++] = v[k - 3]; pl[pk++] = 0.9f;
                toXYZ(s.lat, s.lon, h, xyz, 0);
                pl[pk++] = xyz[0]; pl[pk++] = xyz[1]; pl[pk++] = xyz[2];
                pl[pk++] = v[k - 5]; pl[pk++] = v[k - 4]; pl[pk++] = v[k - 3]; pl[pk++] = 0f;
                np++;
            }
        }
        ptN = k / 8;
        ptV = buf(Math.max(8, k)); ptV.put(v, 0, k).position(0);
        pillarN = np * 2;
        pillarV = buf(Math.max(7, pk)); pillarV.put(pl, 0, pk).position(0);
    }

    // ---------------- camera ----------------
    void flyTo(float la, float lo, float d) {
        flyLat0 = lat; flyLon0 = lon; flyD0 = dist;
        float dl = lo - lon;
        while (dl > 180) dl -= 360;
        while (dl < -180) dl += 360;
        flyLat1 = la; flyLon1 = lon + dl; flyD1 = d;
        flyT = 0;
        vLat = vLon = 0;
    }
    boolean flying() { return flyT >= 0; }

    private void step(float dt) {
        if (flyT >= 0) {
            flyT += dt / 1.3f;
            float t = Math.min(1f, flyT), e = t < 0.5f ? 4 * t * t * t : 1 - (float) Math.pow(-2 * t + 2, 3) / 2;
            float lift = (float) Math.sin(Math.PI * t) * Math.min(1.2f, Math.abs(flyLon1 - flyLon0) / 120f + Math.abs(flyLat1 - flyLat0) / 90f);
            lat = flyLat0 + (flyLat1 - flyLat0) * e;
            lon = flyLon0 + (flyLon1 - flyLon0) * e;
            dist = flyD0 + (flyD1 - flyD0) * e + lift;          // (on prend de la hauteur pour les grands vols)
            if (t >= 1) flyT = -1;
        } else if (!touching) {
            lon += vLon * dt; lat += vLat * dt;
            float k = (float) Math.exp(-dt * 2.6);
            vLon *= k; vLat *= k;
            if (autoTurn && Math.abs(vLon) < 0.5f && System.currentTimeMillis() - lastTouchMs > 6000) lon += dt * 3f;   // (tourne doucement)
        }
        if (lat > 85) lat = 85;
        if (lat < -85) lat = -85;
        if (lon > 180) lon -= 360;
        if (lon < -180) lon += 360;
    }

    /** direction du soleil (repere de la Terre) : declinaison du jour, longitude selon l'heure UTC */
    private static void sun(float[] o) {
        Calendar c = Calendar.getInstance(TimeZone.getTimeZone("UTC"));
        int day = c.get(Calendar.DAY_OF_YEAR);
        float hours = c.get(Calendar.HOUR_OF_DAY) + c.get(Calendar.MINUTE) / 60f;
        float decl = (float) (-23.44 * Math.cos(2 * Math.PI / 365 * (day + 10)));
        float slon = -15f * (hours - 12f);
        toXYZ(decl, slon, 1f, o, 0);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        long now = System.nanoTime();
        float dt = lastNs == 0 ? 0.016f : Math.min(0.05f, (now - lastNs) / 1e9f);
        lastNs = now;
        time += dt;
        step(dt);
        if (builtVersion != version) buildStations();

        Matrix.setLookAtM(view, 0, 0, 0, dist, 0, 0, 0, 0, 1, 0);
        Matrix.setIdentityM(model, 0);
        Matrix.rotateM(model, 0, lat, 1, 0, 0);
        Matrix.rotateM(model, 0, -lon, 0, 1, 0);
        Matrix.multiplyMM(mv, 0, view, 0, model, 0);
        Matrix.multiplyMM(mvp, 0, proj, 0, mv, 0);
        Matrix.multiplyMM(vp, 0, proj, 0, view, 0);

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        float zoom = (3.2f / dist);
        float dens = ctx.getResources().getDisplayMetrics().density;

        uploadTextures();
        // ciel (Voie lactee) puis etoiles : fixes, tournent avec la Terre
        GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        Matrix.setLookAtM(tmp, 0, 0, 0, 0.001f, 0, 0, 0, 0, 1, 0);
        Matrix.multiplyMM(skyMv, 0, tmp, 0, model, 0);
        Matrix.multiplyMM(skyMvp, 0, proj, 0, skyMv, 0);
        if (texDone) {
            GLES20.glDisable(GLES20.GL_BLEND);
            GLES20.glUseProgram(skyProg);
            GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(skyProg, "uMVP"), 1, false, skyMvp, 0);
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, skyTex);
            GLES20.glUniform1i(GLES20.glGetUniformLocation(skyProg, "uSky"), 0);
            drawSphere(skyProg);
        }
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE);
        drawPoints(starV, starN, skyMvp, skyMv, dens * 0.8f, 2f, true);

        // halo d'atmosphere (carre a l'ecran autour de la Terre)
        double ang = Math.asin(1.0 / dist);
        float rs = (float) (Math.tan(ang) / Math.tan(Math.toRadians(FOV / 2)));
        float aspect = (float) width / Math.max(1, height);
        GLES20.glUseProgram(haloProg);
        GLES20.glUniform2f(GLES20.glGetUniformLocation(haloProg, "uR"), rs * 1.35f / aspect, rs * 1.35f);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(haloProg, "uK"), 1.35f);
        int hp = GLES20.glGetAttribLocation(haloProg, "aPos");
        haloV.position(0);
        GLES20.glVertexAttribPointer(hp, 2, GLES20.GL_FLOAT, false, 8, haloV);
        GLES20.glEnableVertexAttribArray(hp);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GLES20.glDisableVertexAttribArray(hp);

        // la Terre
        GLES20.glDisable(GLES20.GL_BLEND);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glUseProgram(earthProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(earthProg, "uMVP"), 1, false, mvp, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(earthProg, "uMV"), 1, false, mv, 0);
        sun(sunV);
        GLES20.glUniform3f(GLES20.glGetUniformLocation(earthProg, "uSun"), sunV[0], sunV[1], sunV[2]);
        float[] inv = tmp;                                      // (camera dans le repere de la Terre : reflet du soleil)
        Matrix.invertM(inv, 0, model, 0);
        float[] cam = { 0, 0, dist, 1 }, cm = new float[4];
        Matrix.multiplyMV(cm, 0, inv, 0, cam, 0);
        GLES20.glUniform3f(GLES20.glGetUniformLocation(earthProg, "uCam"), cm[0], cm[1], cm[2]);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(earthProg, "uReady"), texDone ? 1f : 0f);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, sdfTex);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(earthProg, "uSdf"), 0);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE1);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, nightTex);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(earthProg, "uNight"), 1);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE2);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, reliefTex);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(earthProg, "uRelief"), 2);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        drawSphere(earthProg);

        // frontieres (fines)
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA);
        GLES20.glDepthMask(false);
        if (borderN > 0 && dist < 4.2f) drawLines(borderV, borderN, GLES20.GL_LINES, false, Math.max(1f, 1.2f * dens));

        // radios : colonnes puis points (lumiere qui s'ajoute)
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA);
        GLES20.glDepthMask(false);
        if (pillarN > 0) drawLines(pillarV, pillarN, GLES20.GL_LINES, false, 2f * dens);
        GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE);
        if (ptN > 0) drawPoints(ptV, ptN, mvp, mv, dens * (0.62f + 0.14f * Math.min(zoom, 2.6f)), 0f, false);   // (taille presque fixe : nette de pres)

        // anneaux et arc
        List<RadioDir.Station> st = stations;
        int sel = selected, play = playing;
        markV.position(0);
        int mk = 0;
        float[] p = new float[3];
        if (st != null && sel >= 0 && sel < st.size()) { mk += mark(st.get(sel), 0xFFFFFFFF, 46, p); }
        if (st != null && play >= 0 && play < st.size() && play != sel) { mk += mark(st.get(play), Neon.CYAN, 40, p); }
        if (mk > 0) { markV.position(0); drawPoints(markV, mk, mvp, mv, dens * 0.9f, 1f, false); }
        if (st != null && sel >= 0 && play >= 0 && sel != play && sel < st.size() && play < st.size()) arc(st.get(play), st.get(sel), dens);
        GLES20.glDepthMask(true);

        // etiquette de la radio choisie (position a l'ecran, pour l'interface)
        if (st != null && sel >= 0 && sel < st.size()) {
            RadioDir.Station x = st.get(sel);
            float[] w = new float[4], c = new float[4], e = new float[4];
            toXYZ(x.lat, x.lon, 1.004f, w, 0);
            w[3] = 1;
            Matrix.multiplyMV(c, 0, mvp, 0, w, 0);
            Matrix.multiplyMV(e, 0, mv, 0, w, 0);
            float[] nn = new float[4];
            w[3] = 0;
            Matrix.multiplyMV(nn, 0, mv, 0, w, 0);
            boolean front = -(nn[0] * e[0] + nn[1] * e[1] + nn[2] * e[2]) > 0;
            if (front && c[3] > 0) { selX = (c[0] / c[3] + 1) / 2 * width; selY = (1 - c[1] / c[3]) / 2 * height; }
            else selX = selY = -1;
        } else selX = selY = -1;
        FrameListener fl = frameListener;
        if (fl != null) fl.onFrame();
    }

    private int mark(RadioDir.Station s, int col, float size, float[] p) {
        toXYZ(s.lat, s.lon, 1.006f, p, 0);
        markV.put(p[0]).put(p[1]).put(p[2]);
        markV.put(((col >> 16) & 255) / 255f).put(((col >> 8) & 255) / 255f).put((col & 255) / 255f).put(1f);
        markV.put(size);
        return 1;
    }

    /** arc en grand cercle (radio de la console -> radio choisie), en pointilles qui avancent */
    private void arc(RadioDir.Station a, RadioDir.Station b, float dens) {
        float[] pa = new float[3], pb = new float[3];
        toXYZ(a.lat, a.lon, 1f, pa, 0);
        toXYZ(b.lat, b.lon, 1f, pb, 0);
        double dot = Math.max(-1, Math.min(1, pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2])), om = Math.acos(dot);
        if (om < 1e-3) return;
        arcV.position(0);
        int n = 64;
        for (int i = 0; i <= n; i++) {
            double t = (double) i / n, s0 = Math.sin((1 - t) * om) / Math.sin(om), s1 = Math.sin(t * om) / Math.sin(om);
            float h = 1.004f + (float) (Math.sin(Math.PI * t) * Math.min(0.35, om * 0.22));
            float x = (float) (pa[0] * s0 + pb[0] * s1), y = (float) (pa[1] * s0 + pb[1] * s1), z = (float) (pa[2] * s0 + pb[2] * s1);
            float l = (float) Math.sqrt(x * x + y * y + z * z);
            arcV.put(x / l * h).put(y / l * h).put(z / l * h);
            float tt = (float) t;
            arcV.put(0.13f + 0.87f * tt).put(0.91f - 0.73f * tt).put(1f - 0.18f * tt).put(tt);   // (alpha porte la position : pointilles)
        }
        arcV.position(0);
        drawLines(arcV, n + 1, GLES20.GL_LINE_STRIP, true, 3f * dens);
    }

    private void drawPoints(FloatBuffer v, int n, float[] m, float[] mvm, float scale, float mode, boolean stars) {
        GLES20.glUseProgram(ptProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(ptProg, "uMVP"), 1, false, m, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(ptProg, "uMV"), 1, false, mvm, 0);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uScale"), scale);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uMode"), mode);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(ptProg, "uTime"), time);
        int ap = GLES20.glGetAttribLocation(ptProg, "aPos"), ac = GLES20.glGetAttribLocation(ptProg, "aCol"), as = GLES20.glGetAttribLocation(ptProg, "aSize");
        v.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 32, v);
        v.position(3);
        GLES20.glVertexAttribPointer(ac, 4, GLES20.GL_FLOAT, false, 32, v);
        v.position(7);
        GLES20.glVertexAttribPointer(as, 1, GLES20.GL_FLOAT, false, 32, v);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(ac);
        GLES20.glEnableVertexAttribArray(as);
        GLES20.glDrawArrays(GLES20.GL_POINTS, 0, n);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(ac);
        GLES20.glDisableVertexAttribArray(as);
        v.position(0);
    }

    private void drawLines(FloatBuffer v, int n, int mode, boolean dash, float width) {
        GLES20.glUseProgram(lineProg);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(lineProg, "uMVP"), 1, false, mvp, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(lineProg, "uMV"), 1, false, mv, 0);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(lineProg, "uTime"), time);
        GLES20.glUniform1f(GLES20.glGetUniformLocation(lineProg, "uDash"), dash ? 1f : 0f);
        GLES20.glLineWidth(width);
        int ap = GLES20.glGetAttribLocation(lineProg, "aPos"), ac = GLES20.glGetAttribLocation(lineProg, "aCol");
        v.position(0);
        GLES20.glVertexAttribPointer(ap, 3, GLES20.GL_FLOAT, false, 28, v);
        v.position(3);
        GLES20.glVertexAttribPointer(ac, 4, GLES20.GL_FLOAT, false, 28, v);
        GLES20.glEnableVertexAttribArray(ap);
        GLES20.glEnableVertexAttribArray(ac);
        GLES20.glDrawArrays(mode, 0, n);
        GLES20.glDisableVertexAttribArray(ap);
        GLES20.glDisableVertexAttribArray(ac);
        v.position(0);
    }

    /** la radio la plus proche du doigt (ecran), parmi celles de face et visibles ; -1 si aucune (fil de l'interface) */
    int pick(float x, float y, float radiusPx) {
        List<RadioDir.Station> st = stations;
        boolean[] vis = visible;
        if (st == null) return -1;
        float[] m = mvp.clone(), e = mv.clone();
        int w = width, h = height, best = -1;
        float bd = radiusPx * radiusPx, bestScore = Float.MAX_VALUE;
        float[] p = new float[3];
        for (int i = 0; i < st.size(); i++) {
            if (vis != null && (i >= vis.length || !vis[i])) continue;
            RadioDir.Station s = st.get(i);
            toXYZ(s.lat, s.lon, 1.004f, p, 0);
            float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw <= 0) continue;
            float cx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw, cy = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
            float sx = (cx + 1) / 2 * w, sy = (1 - cy) / 2 * h, dx = sx - x, dy = sy - y, d = dx * dx + dy * dy;
            if (d > bd) continue;
            float ex = e[0] * p[0] + e[4] * p[1] + e[8] * p[2] + e[12], ey = e[1] * p[0] + e[5] * p[1] + e[9] * p[2] + e[13], ez = e[2] * p[0] + e[6] * p[1] + e[10] * p[2] + e[14];
            float nx = e[0] * p[0] + e[4] * p[1] + e[8] * p[2], ny = e[1] * p[0] + e[5] * p[1] + e[9] * p[2], nz = e[2] * p[0] + e[6] * p[1] + e[10] * p[2];
            if (-(nx * ex + ny * ey + nz * ez) <= 0) continue;   // (de l'autre cote de la Terre)
            float score = d / (1f + 0.25f * (float) Math.log(1 + s.clicks));   // (la plus proche ; a egalite, la plus ecoutee)
            if (score < bestScore) { bestScore = score; best = i; }
        }
        return best;
    }
}
