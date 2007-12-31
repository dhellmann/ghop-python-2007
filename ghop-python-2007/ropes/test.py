#!/usr/bin/python
import unittest
import ropes

para1='Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Donec tortor elit, tincidunt a, sodales vitae, aliquet vel, risus. Cras fringilla dapibus enim. Morbi ultrices pulvinar orci. Praesent placerat massa a nulla. Integer blandit tincidunt lorem. Proin imperdiet convallis dolor. Aliquam ac nisi. Morbi dapibus luctus risus. Pellentesque viverra, arcu at mattis tempor, nisl leo molestie sem, in auctor mi augue a nisl. Donec metus elit, egestas vel, convallis vel, condimentum a, mi. Suspendisse ultricies pede in justo. Mauris at justo.'
para2='Cras ac felis. Proin eget metus. Nullam tristique tristique leo. Integer ullamcorper viverra diam. Sed faucibus fringilla enim. Fusce augue. Nunc a dolor. Sed rhoncus ligula a orci. Integer diam felis, semper at, tempor at, dapibus id, orci. Nam eget arcu id lorem congue vehicula. Proin facilisis libero eget neque. Sed congue lobortis nunc.'
para3='In sapien. Fusce viverra lectus in tortor. Nam lobortis massa quis pede. Praesent et est vel purus consequat placerat. In tellus ligula, viverra nec, mattis eu, pretium ac, nulla. Etiam ultrices nisi vel sapien. Quisque leo mauris, vehicula et, condimentum tristique, luctus a, magna. Donec id ante. Proin felis urna, fringilla ut, ullamcorper sed, molestie at, arcu. Curabitur est odio, iaculis ac, dapibus at, ultrices id, risus. Nulla accumsan. Morbi sagittis porta nibh.'
para4='Proin molestie fermentum diam. Nullam imperdiet. Mauris felis. Donec ut quam. Suspendisse potenti. Sed sapien nunc, pulvinar eget, feugiat et, imperdiet eget, sem. Vivamus porttitor eros aliquam ligula. Suspendisse ultrices consectetuer nulla. Sed est. Donec sem dolor, facilisis a, lacinia eget, lobortis nonummy, ipsum. Nulla facilisi. Donec et sem. Fusce in nibh a nunc luctus pharetra. Pellentesque lorem metus, vulputate aliquet, elementum non, egestas a, tortor.'
para5='Curabitur ligula mi, varius aliquet, consectetuer non, blandit et, leo. Nulla egestas tristique nibh. In ultrices accumsan magna. Aenean non augue. Fusce nonummy turpis sit amet est. Integer aliquet vulputate diam. Vivamus urna neque, vestibulum id, tincidunt sed, molestie vitae, nisi. Phasellus nonummy urna ac nibh. Phasellus in arcu sit amet metus consequat molestie. Cum sociis natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Cras lacinia dui ac lorem. Nam in leo sed justo mattis ultricies. Proin vitae urna. Quisque commodo tincidunt lacus. Mauris luctus felis a mi. Vestibulum posuere. Duis vel metus malesuada velit laoreet porttitor. Proin suscipit viverra diam. Morbi fringilla vestibulum eros.'

class TestRopes(unittest.TestCase):
    def testAppends(self):
        r1=ropes.Rope()
        r2=ropes.Rope()
        r2.append(para4)
        r2+=para5
        r1.append(para1)
        r1.append(para2)
        r1.append(para3)
        r1+=r2
        self.assertEqual(str(r1),para1+para2+para3+para4+para5);

    def testBalance(self):
        r1=ropes.Rope()
        r1.append(para1)
        r1.append(para2)
        r1.append(para3)
        r1.append(para4)
        r1.append(para5)
        r1.balance()
        self.assertEqual(str(r1),para1+para2+para3+para4+para5);

    def testRepetition(self):
        r1=ropes.Rope('hello')
        r1*=100
        self.assertEqual(str(r1),'hello'*100)

    def testLength(self):
        r1=ropes.Rope('hell')
        r1+='o, w'
        r1+='orld'
        self.assertEqual(len(r1), 12)

    def testLength(self):
        r1=ropes.Rope('hello')
        r1*=100
        self.assertEqual(len(r1), 500)

    def testLeftAndRight(self):
        r1=ropes.Rope('this')
        r1+=' is'
        r1+=' a'
        r1+=' test'
        self.assertEqual(str(r1.right.left), ' is')
        self.assertEqual(r1.right.right.type, ropes.ROPE_CONCAT_NODE)

if __name__=="__main__":
    unittest.main()
