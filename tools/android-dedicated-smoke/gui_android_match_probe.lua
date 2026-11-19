function widget:GetInfo()
	return {
		name = "Android Match Probe",
		desc = "Local Android test bridge for commander position and move orders",
		enabled = true,
	}
end

local listener
local client
local pending = ""

local function commander()
	local units = Spring.GetTeamUnits(Spring.GetMyTeamID()) or {}
	for _, id in ipairs(units) do
		local def = UnitDefs[Spring.GetUnitDefID(id)]
		if def and def.customParams.iscommander then
			return id
		end
	end
end

local function handle(request)
	if request == "TERRAIN" then
		local samples = {"TERRAIN", Game.mapSizeX, Game.mapSizeZ}
		for row = 0, 24 do
			for col = 0, 24 do
				samples[#samples + 1] = math.floor(Spring.GetGroundHeight(
					col * Game.mapSizeX / 24, row * Game.mapSizeZ / 24))
			end
		end
		return table.concat(samples, " ") .. "\n"
	end
	local id = commander()
	if not id then
		return "WAIT\n"
	end
	local x, y, z = Spring.GetUnitPosition(id)
	if not x then
		return "WAIT\n"
	end
	if request == "STATE" then
		return ("STATE %d %d %.2f %.2f %.2f\n"):format(Spring.GetGameFrame(), id, x, y, z)
	end
	local tx, tz = request:match("^MOVE ([%d.]+) ([%d.]+)$")
	tx, tz = tonumber(tx), tonumber(tz)
	if tx and tz and tx >= 0 and tx <= Game.mapSizeX and tz >= 0 and tz <= Game.mapSizeZ then
		local accepted = Spring.GiveOrderToUnit(id, CMD.MOVE, {tx, Spring.GetGroundHeight(tx, tz), tz}, {})
		return accepted and "OK\n" or "DENIED\n"
	end
	return "INVALID\n"
end

function widget:Initialize()
	if not Platform.isHeadless or not socket then
		widgetHandler:RemoveWidget()
		return
	end
	listener = socket.tcp()
	local ok, err = listener:bind("127.0.0.1", 8454)
	if ok then
		ok, err = listener:listen(1)
	end
	if not ok then
		Spring.Echo("[Android match probe] " .. tostring(err))
		widgetHandler:RemoveWidget()
		return
	end
	listener:settimeout(0)
	Spring.Echo("[Android match probe] listening on 127.0.0.1:8454")
end

function widget:Update()
	if not client then
		client = listener:accept()
		if not client then
			return
		end
		client:settimeout(0)
		pending = ""
	end
	for _ = 1, 64 do
		local byte, err = client:receive(1)
		if not byte then
			if err ~= "timeout" then
				client:close()
				client = nil
			end
			return
		end
		if byte == "\n" or #pending >= 63 then
			client:send(byte == "\n" and handle(pending) or "INVALID\n")
			client:close()
			client = nil
			return
		end
		pending = pending .. byte
	end
end

function widget:Shutdown()
	if client then
		client:close()
	end
	if listener then
		listener:close()
	end
end
