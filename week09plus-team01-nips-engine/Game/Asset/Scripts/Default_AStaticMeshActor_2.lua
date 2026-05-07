function BeginPlay(owner)
  print("BeginPlay:", owner:GetName())

  StartCoroutine(function()
    print("[Coroutine A] start")

    wait(1.0)
    print("[Coroutine A] after 1 sec")

    wait(2.0)
    print("[Coroutine A] after 3 sec total")

    wait(0.5)
    print("[Coroutine A] end")
  end)

  print("BeginPlay end")
end