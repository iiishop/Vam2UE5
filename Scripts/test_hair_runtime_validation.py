import unittest
from hair_runtime_validation import shader_failures

class ShaderValidationTests(unittest.TestCase):
    def test_warning_severity_still_fails(self):
        self.assertEqual(len(shader_failures('LogNiagara: Warning: error X4600: Shader uses too many (4098) indexable literal values')),1)

    def test_real_compile_error(self):
        self.assertTrue(shader_failures('LogShaderCompilers: Error: Failed to compile shader'))

    def test_benign_warning(self):
        self.assertFalse(shader_failures('LogNiagara: Warning: warning X3556: integer divides may be slow'))

if __name__=='__main__':unittest.main()
