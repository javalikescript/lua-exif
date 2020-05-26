local exifLib = require('exif')

local function printTable(t, i, p)
    i = i or '  '
    p = p or i
    for name, value in pairs(t) do
        if (type(value) == 'table') then
            print(p..tostring(name)..':')
            printTable(value, p..i)
        else
            print(p..tostring(name)..' = '..tostring(value))
        end
    end
end

-- see https://github.com/ianare/exif-samples/tree/master/jpg
local filename = arg[1] or 'lua-exif/Sony_HDR-HC3.jpg'

local loader = exifLib.loader_new()

print('reading '..filename)
local fd = io.open(filename, 'rb')
repeat
    local chunk = fd:read(2048)
until not exifLib.loader_write(loader, chunk)
fd:close()

local data = exifLib.loader_get_data(loader)

if data then
    print('EXIF:')
    local t = exifLib.data_to_table(data)
    printTable(t)

    local newData = exifLib.data_new()
    exifLib.data_from_table(newData, t)
else
    print('no EXIF data found')
end
