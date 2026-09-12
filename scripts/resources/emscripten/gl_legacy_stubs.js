/**
 * Stub only fixed-function entry points that WebGL + LEGACY_GL_EMULATION still
 * leave as TODO (they log and can stall). Do NOT override real legacy helpers
 * like glEnd / glOrtho / glEnableClientState — those break MAME's OGL renderer.
 */
mergeInto(LibraryManager.library, {
  glShadeModel: function () {},
  glClearDepth: function () {},
  glDepthFunc: function () {},
  glHint: function () {},
  glGetTexLevelParameteriv: function () {},
  glPushAttrib: function () {},
  glPopAttrib: function () {},
  glPointSize: function () {}
});
