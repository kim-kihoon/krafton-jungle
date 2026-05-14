-- LuaScriptComponent contract
-- owner: AActor bound from C++
-- otherActor: AActor or nil
-- hit: FHitResult
-- Log(message): writes to the editor console
-- StartCoroutine(function() ... end), wait(seconds): coroutine helpers

function BeginPlay(owner)
    print("BeginPlay", owner:GetName())

    StartCoroutine(function()
        print("coroutine start")

        wait(1.0)
        print("after 1 sec")

        wait(2.0)
        print("after 3 sec total")
    end)
end

function EndPlay(owner)
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

function OnInteract(owner, interactor)
end

function Tick(owner, deltaTime)
end
