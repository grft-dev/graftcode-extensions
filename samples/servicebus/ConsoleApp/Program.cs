graft.nuget.SimpleNetClass.GraftConfig.SetConfig("./config.json");

for (int i = 0; i < 50; i++)
{
    var result = graft.nuget.SimpleNetClass.Class1.Multiply(i, i + 1);
    Console.WriteLine($"The result of multiplying {i} and {i + 1} is: {result}");
}