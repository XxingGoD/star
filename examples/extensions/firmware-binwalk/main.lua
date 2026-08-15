local extension = { commands = {} }

local function require_box(ctx)
    if ctx.box == nil then
        error("firmware.binwalk requires --box <box-ref>", 2)
    end
end

function extension.commands.scan(ctx, args)
    require_box(ctx)
    return ctx:call("box.exec", {
        ref = ctx.box,
        argv = { "binwalk", args.path },
        workdir = ctx.workspace.box_path,
        capture = true
    })
end

function extension.commands.extract(ctx, args)
    require_box(ctx)
    return ctx:call("box.exec", {
        ref = ctx.box,
        argv = { "binwalk", "-e", args.path, "--run-as=root" },
        workdir = ctx.workspace.box_path,
        capture = true
    })
end

return extension
