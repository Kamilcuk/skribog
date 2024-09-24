
lua <<EOF
local lspconfig = require('lspconfig')

-- lspconfig.arduino_language_server.setup {
--   filetypes = {
--     "arduino",
--   },
--   cmd = {
--     "arduino-language-server",
--     "-clangd",
--     "clangd",
--     "-cli",
--     "arduino-cli",
--     "-cli-config",
--     "$HOME/.arduino15/arduino-cli.yaml",
--     "-fqbn",
--     "esp8266:esp8266:nodemcuv2",
--   },
-- }

EOF

