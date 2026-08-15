local extension = { commands = {} }

function extension.commands.unpack(ctx, args)
    ctx:progress("extract", 10, "starting firmware extraction")
    local result = ctx:call("tool.invoke", {
        tool = "firmware.binwalk",
        command = "extract",
        args = { path = args.path }
    })
    ctx:progress("extract", 100, "firmware extraction complete")
    return {
        path = args.path,
        exitCode = result.exitCode,
        output = result.output
    }
end

return extension
