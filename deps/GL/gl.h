#ifndef __gl_h_
#define __gl_h_

#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__) && !defined(__SC__)
#define WIN32_LEAN_AND_MEAN 1
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;

#define GL_FALSE                          0
#define GL_TRUE                           1

#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006
#define GL_QUADS                          0x0007
#define GL_QUAD_STRIP                     0x0008
#define GL_POLYGON                        0x0009

#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_COLOR_BUFFER_BIT               0x00004000

#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307
#define GL_SRC_ALPHA_SATURATE             0x0308

#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408

#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_TEXTURE_2D                     0x0DE1

#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_PACK_ALIGNMENT                 0x0D05

#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803

#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703

#define GL_REPEAT                         0x2901
#define GL_CLAMP                          0x2900
#define GL_CLAMP_TO_EDGE                  0x812F

#define GL_TEXTURE_ENV                    0x2300
#define GL_TEXTURE_ENV_MODE               0x2200
#define GL_MODULATE                       0x2100
#define GL_DECAL                          0x2101
#define GL_REPLACE                        0x1E01

#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_LUMINANCE                      0x1909
#define GL_LUMINANCE_ALPHA                0x190A
#define GL_UNSIGNED_BYTE                  0x1401

#define GL_FOG                            0x0B60
#define GL_FOG_MODE                       0x0B65
#define GL_FOG_START                      0x0B63
#define GL_FOG_END                        0x0B64
#define GL_FOG_COLOR                      0x0B66
#define GL_FOG_HINT                       0x0C54
#define GL_NICEST                         0x1102
#define GL_FASTEST                        0x1101
#define GL_DONT_CARE                      0x1100

#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_TEXTURE                        0x1702

#define GL_LINE_SMOOTH                    0x0B20
#define GL_POLYGON_SMOOTH                 0x0B41
#define GL_POLYGON_OFFSET_FILL            0x8037
#define GL_POLYGON_OFFSET_LINE            0x2A02

GLAPI void APIENTRY glClearIndex( GLfloat c );
GLAPI void APIENTRY glClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha );
GLAPI void APIENTRY glClear( GLbitfield mask );
GLAPI void APIENTRY glIndexMask( GLuint mask );
GLAPI void APIENTRY glColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha );
GLAPI void APIENTRY glAlphaFunc( GLenum func, GLclampf ref );
GLAPI void APIENTRY glBlendFunc( GLenum sfactor, GLenum dfactor );
GLAPI void APIENTRY glLogicOp( GLenum opcode );
GLAPI void APIENTRY glCullFace( GLenum mode );
GLAPI void APIENTRY glFrontFace( GLenum mode );
GLAPI void APIENTRY glPointSize( GLfloat size );
GLAPI void APIENTRY glLineWidth( GLfloat width );
GLAPI void APIENTRY glLineStipple( GLint factor, GLushort pattern );
GLAPI void APIENTRY glPolygonMode( GLenum face, GLenum mode );
GLAPI void APIENTRY glPolygonOffset( GLfloat factor, GLfloat units );
GLAPI void APIENTRY glPolygonStipple( const GLubyte *mask );
GLAPI void APIENTRY glGetPolygonStipple( GLubyte *mask );
GLAPI void APIENTRY glEdgeFlag( GLboolean flag );
GLAPI void APIENTRY glEdgeFlagv( const GLboolean *flag );
GLAPI void APIENTRY glScissor( GLint x, GLint y, GLsizei width, GLsizei height );
GLAPI void APIENTRY glClipPlane( GLenum plane, const GLdouble *equation );
GLAPI void APIENTRY glGetClipPlane( GLenum plane, GLdouble *equation );
GLAPI void APIENTRY glDrawBuffer( GLenum mode );
GLAPI void APIENTRY glReadBuffer( GLenum mode );
GLAPI void APIENTRY glEnable( GLenum cap );
GLAPI void APIENTRY glDisable( GLenum cap );
GLAPI GLboolean APIENTRY glIsEnabled( GLenum cap );
GLAPI void APIENTRY glEnableClientState( GLenum cap );
GLAPI void APIENTRY glDisableClientState( GLenum cap );
GLAPI void APIENTRY glGetBooleanv( GLenum pname, GLboolean *params );
GLAPI void APIENTRY glGetDoublev( GLenum pname, GLdouble *params );
GLAPI void APIENTRY glGetFloatv( GLenum pname, GLfloat *params );
GLAPI void APIENTRY glGetIntegerv( GLenum pname, GLint *params );
GLAPI void APIENTRY glPushAttrib( GLbitfield mask );
GLAPI void APIENTRY glPopAttrib( void );
GLAPI void APIENTRY glPushClientAttrib( GLbitfield mask );
GLAPI void APIENTRY glPopClientAttrib( void );
GLAPI GLenum APIENTRY glGetError( void );
GLAPI const GLubyte * APIENTRY glGetString( GLenum name );
GLAPI void APIENTRY glFinish( void );
GLAPI void APIENTRY glFlush( void );
GLAPI void APIENTRY glHint( GLenum target, GLenum mode );

GLAPI void APIENTRY glClearDepth( GLclampd depth );
GLAPI void APIENTRY glDepthFunc( GLenum func );
GLAPI void APIENTRY glDepthMask( GLboolean flag );
GLAPI void APIENTRY glDepthRange( GLclampd near_val, GLclampd far_val );

GLAPI void APIENTRY glMatrixMode( GLenum mode );
GLAPI void APIENTRY glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val );
GLAPI void APIENTRY glFrustum( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val );
GLAPI void APIENTRY glViewport( GLint x, GLint y, GLsizei width, GLsizei height );
GLAPI void APIENTRY glPushMatrix( void );
GLAPI void APIENTRY glPopMatrix( void );
GLAPI void APIENTRY glLoadIdentity( void );
GLAPI void APIENTRY glLoadMatrixd( const GLdouble *m );
GLAPI void APIENTRY glLoadMatrixf( const GLfloat *m );
GLAPI void APIENTRY glMultMatrixd( const GLdouble *m );
GLAPI void APIENTRY glMultMatrixf( const GLfloat *m );
GLAPI void APIENTRY glRotated( GLdouble angle, GLdouble x, GLdouble y, GLdouble z );
GLAPI void APIENTRY glRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z );
GLAPI void APIENTRY glScaled( GLdouble x, GLdouble y, GLdouble z );
GLAPI void APIENTRY glScalef( GLfloat x, GLfloat y, GLfloat z );
GLAPI void APIENTRY glTranslated( GLdouble x, GLdouble y, GLdouble z );
GLAPI void APIENTRY glTranslatef( GLfloat x, GLfloat y, GLfloat z );

GLAPI void APIENTRY glBegin( GLenum mode );
GLAPI void APIENTRY glEnd( void );
GLAPI void APIENTRY glVertex2d( GLdouble x, GLdouble y );
GLAPI void APIENTRY glVertex2f( GLfloat x, GLfloat y );
GLAPI void APIENTRY glVertex2i( GLint x, GLint y );
GLAPI void APIENTRY glVertex2s( GLshort x, GLshort y );
GLAPI void APIENTRY glVertex3d( GLdouble x, GLdouble y, GLdouble z );
GLAPI void APIENTRY glVertex3f( GLfloat x, GLfloat y, GLfloat z );
GLAPI void APIENTRY glVertex3i( GLint x, GLint y, GLint z );
GLAPI void APIENTRY glVertex3s( GLshort x, GLshort y, GLshort z );
GLAPI void APIENTRY glVertex4d( GLdouble x, GLdouble y, GLdouble z, GLdouble w );
GLAPI void APIENTRY glVertex4f( GLfloat x, GLfloat y, GLfloat z, GLfloat w );
GLAPI void APIENTRY glVertex4i( GLint x, GLint y, GLint z, GLint w );
GLAPI void APIENTRY glVertex4s( GLshort x, GLshort y, GLshort z, GLshort w );
GLAPI void APIENTRY glVertex2dv( const GLdouble *v );
GLAPI void APIENTRY glVertex2fv( const GLfloat *v );
GLAPI void APIENTRY glVertex2iv( const GLint *v );
GLAPI void APIENTRY glVertex2sv( const GLshort *v );
GLAPI void APIENTRY glVertex3dv( const GLdouble *v );
GLAPI void APIENTRY glVertex3fv( const GLfloat *v );
GLAPI void APIENTRY glVertex3iv( const GLint *v );
GLAPI void APIENTRY glVertex3sv( const GLshort *v );
GLAPI void APIENTRY glVertex4dv( const GLdouble *v );
GLAPI void APIENTRY glVertex4fv( const GLfloat *v );
GLAPI void APIENTRY glVertex4iv( const GLint *v );
GLAPI void APIENTRY glVertex4sv( const GLshort *v );
GLAPI void APIENTRY glNormal3b( GLbyte nx, GLbyte ny, GLbyte nz );
GLAPI void APIENTRY glNormal3d( GLdouble nx, GLdouble ny, GLdouble nz );
GLAPI void APIENTRY glNormal3f( GLfloat nx, GLfloat ny, GLfloat nz );
GLAPI void APIENTRY glNormal3i( GLint nx, GLint ny, GLint nz );
GLAPI void APIENTRY glNormal3s( GLshort nx, GLshort ny, GLshort nz );
GLAPI void APIENTRY glNormal3bv( const GLbyte *v );
GLAPI void APIENTRY glNormal3dv( const GLdouble *v );
GLAPI void APIENTRY glNormal3fv( const GLfloat *v );
GLAPI void APIENTRY glNormal3iv( const GLint *v );
GLAPI void APIENTRY glNormal3sv( const GLshort *v );
GLAPI void APIENTRY glColor3b( GLbyte red, GLbyte green, GLbyte blue );
GLAPI void APIENTRY glColor3d( GLdouble red, GLdouble green, GLdouble blue );
GLAPI void APIENTRY glColor3f( GLfloat red, GLfloat green, GLfloat blue );
GLAPI void APIENTRY glColor3i( GLint red, GLint green, GLint blue );
GLAPI void APIENTRY glColor3s( GLshort red, GLshort green, GLshort blue );
GLAPI void APIENTRY glColor3ub( GLubyte red, GLubyte green, GLubyte blue );
GLAPI void APIENTRY glColor3ui( GLuint red, GLuint green, GLuint blue );
GLAPI void APIENTRY glColor3us( GLushort red, GLushort green, GLushort blue );
GLAPI void APIENTRY glColor4b( GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha );
GLAPI void APIENTRY glColor4d( GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha );
GLAPI void APIENTRY glColor4f( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha );
GLAPI void APIENTRY glColor4i( GLint red, GLint green, GLint blue, GLint alpha );
GLAPI void APIENTRY glColor4s( GLshort red, GLshort green, GLshort blue, GLshort alpha );
GLAPI void APIENTRY glColor4ub( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha );
GLAPI void APIENTRY glColor4ui( GLuint red, GLuint green, GLuint blue, GLuint alpha );
GLAPI void APIENTRY glColor4us( GLushort red, GLushort green, GLushort blue, GLushort alpha );
GLAPI void APIENTRY glColor3bv( const GLbyte *v );
GLAPI void APIENTRY glColor3dv( const GLdouble *v );
GLAPI void APIENTRY glColor3fv( const GLfloat *v );
GLAPI void APIENTRY glColor3iv( const GLint *v );
GLAPI void APIENTRY glColor3sv( const GLshort *v );
GLAPI void APIENTRY glColor3ubv( const GLubyte *v );
GLAPI void APIENTRY glColor3uiv( const GLuint *v );
GLAPI void APIENTRY glColor3usv( const GLushort *v );
GLAPI void APIENTRY glColor4bv( const GLbyte *v );
GLAPI void APIENTRY glColor4dv( const GLdouble *v );
GLAPI void APIENTRY glColor4fv( const GLfloat *v );
GLAPI void APIENTRY glColor4iv( const GLint *v );
GLAPI void APIENTRY glColor4sv( const GLshort *v );
GLAPI void APIENTRY glColor4ubv( const GLubyte *v );
GLAPI void APIENTRY glColor4uiv( const GLuint *v );
GLAPI void APIENTRY glColor4usv( const GLushort *v );
GLAPI void APIENTRY glTexCoord1d( GLdouble s );
GLAPI void APIENTRY glTexCoord1f( GLfloat s );
GLAPI void APIENTRY glTexCoord1i( GLint s );
GLAPI void APIENTRY glTexCoord1s( GLshort s );
GLAPI void APIENTRY glTexCoord2d( GLdouble s, GLdouble t );
GLAPI void APIENTRY glTexCoord2f( GLfloat s, GLfloat t );
GLAPI void APIENTRY glTexCoord2i( GLint s, GLint t );
GLAPI void APIENTRY glTexCoord2s( GLshort s, GLshort t );
GLAPI void APIENTRY glTexCoord3d( GLdouble s, GLdouble t, GLdouble r );
GLAPI void APIENTRY glTexCoord3f( GLfloat s, GLfloat t, GLfloat r );
GLAPI void APIENTRY glTexCoord3i( GLint s, GLint t, GLint r );
GLAPI void APIENTRY glTexCoord3s( GLshort s, GLshort t, GLshort r );
GLAPI void APIENTRY glTexCoord4d( GLdouble s, GLdouble t, GLdouble r, GLdouble q );
GLAPI void APIENTRY glTexCoord4f( GLfloat s, GLfloat t, GLfloat r, GLfloat q );
GLAPI void APIENTRY glTexCoord4i( GLint s, GLint t, GLint r, GLint q );
GLAPI void APIENTRY glTexCoord4s( GLshort s, GLshort t, GLshort r, GLshort q );
GLAPI void APIENTRY glTexCoord1dv( const GLdouble *v );
GLAPI void APIENTRY glTexCoord1fv( const GLfloat *v );
GLAPI void APIENTRY glTexCoord1iv( const GLint *v );
GLAPI void APIENTRY glTexCoord1sv( const GLshort *v );
GLAPI void APIENTRY glTexCoord2dv( const GLdouble *v );
GLAPI void APIENTRY glTexCoord2fv( const GLfloat *v );
GLAPI void APIENTRY glTexCoord2iv( const GLint *v );
GLAPI void APIENTRY glTexCoord2sv( const GLshort *v );
GLAPI void APIENTRY glTexCoord3dv( const GLdouble *v );
GLAPI void APIENTRY glTexCoord3fv( const GLfloat *v );
GLAPI void APIENTRY glTexCoord3iv( const GLint *v );
GLAPI void APIENTRY glTexCoord3sv( const GLshort *v );
GLAPI void APIENTRY glTexCoord4dv( const GLdouble *v );
GLAPI void APIENTRY glTexCoord4fv( const GLfloat *v );
GLAPI void APIENTRY glTexCoord4iv( const GLint *v );
GLAPI void APIENTRY glTexCoord4sv( const GLshort *v );

GLAPI void APIENTRY glTexEnvf( GLenum target, GLenum pname, GLfloat param );
GLAPI void APIENTRY glTexEnvi( GLenum target, GLenum pname, GLint param );
GLAPI void APIENTRY glTexEnvfv( GLenum target, GLenum pname, const GLfloat *params );
GLAPI void APIENTRY glTexEnviv( GLenum target, GLenum pname, const GLint *params );
GLAPI void APIENTRY glTexParameteri( GLenum target, GLenum pname, GLint param );
GLAPI void APIENTRY glTexParameterf( GLenum target, GLenum pname, GLfloat param );
GLAPI void APIENTRY glTexParameteriv( GLenum target, GLenum pname, const GLint *params );
GLAPI void APIENTRY glTexParameterfv( GLenum target, GLenum pname, const GLfloat *params );
GLAPI void APIENTRY glTexImage1D( GLenum target, GLint level, GLint internalFormat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels );
GLAPI void APIENTRY glTexImage2D( GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels );
GLAPI void APIENTRY glBindTexture( GLenum target, GLuint texture );
GLAPI void APIENTRY glGenTextures( GLsizei n, GLuint *textures );
GLAPI void APIENTRY glDeleteTextures( GLsizei n, const GLuint *textures );
GLAPI GLboolean APIENTRY glIsTexture( GLuint texture );
GLAPI void APIENTRY glPixelStoref( GLenum pname, GLfloat param );
GLAPI void APIENTRY glPixelStorei( GLenum pname, GLint param );

GLAPI void APIENTRY glFogi( GLenum pname, GLint param );
GLAPI void APIENTRY glFogf( GLenum pname, GLfloat param );
GLAPI void APIENTRY glFogiv( GLenum pname, const GLint *params );
GLAPI void APIENTRY glFogfv( GLenum pname, const GLfloat *params );

#ifdef __cplusplus
}
#endif

#endif /* __gl_h_ */
