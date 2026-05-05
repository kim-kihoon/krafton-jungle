function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] ChocolateBar BeginPlay owner=nil")
        return
    end

    local itemId = "chocolate_bar"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
